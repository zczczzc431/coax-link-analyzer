`timescale 1ns/1ps
// Candidate native-BRAM adapter. PS owns REQUEST/config/CANCEL. PL owns samples,
// metadata/DONE. Config must be written before REQUEST with a memory barrier.
module adc_trigger_mailbox (
    input ps_clk,input ps_rst_n,input adc_clk,input adc_rst_n,
    input valid,input [13:0] sample_first,input [13:0] sample_second,
    input [31:0] bram_dout,
    output reg [31:0] bram_addr,output reg [31:0] bram_din,
    output reg bram_en,output reg [3:0] bram_we
);
    localparam REQUEST='h2000,DONE='h2004,MAGIC='h2008,COUNT='h200c,
        FEATURE='h2010,CANCEL='h2018,LEVEL='h2020,CTRL='h2024,
        PRE='h2028,TIMEOUT='h202c,RESULT='h2030,TRIGGER_INDEX='h2034,
        MON_CTRL='h2040,MON_STATUS='h2044,MON_BASELINE='h2048,
        MON_METRIC='h204c,MON_EVENT_COUNT='h2050,MON_FAULT_WINDOWS='h2054,
        MON_LAST_DURATION='h2058;
    localparam INIT0=0,INIT1=1,INIT2=2,INIT3=3,INIT4=4,INIT5=5,
        REQ_READ=6,READ_WAIT1=7,READ_WAIT2=8,READ_CHECK=9,
        BUSY_WRITE=10,CFG_READ=11,ARM=12,CANCEL_READ=13,
        SAMPLE_READ=14,SAMPLE_WAIT1=15,SAMPLE_WAIT2=16,SAMPLE_WRITE=17,
        RESULT_WRITE=18,INDEX_WRITE=19,DONE_WRITE=20,
        MON_STATUS_WRITE=21,MON_BASELINE_WRITE=22,MON_METRIC_WRITE=23,
        MON_COUNT_WRITE=24,MON_FAULT_WRITE=25,MON_DURATION_WRITE=26,
        MON_CTRL_INIT=27,MON_CTRL_READ=28,MON_CTRL_ACK=29;
    reg [4:0] state,read_return;
    reg [2:0] cfg_index;
    reg [10:0] output_index;
    reg [31:0] token,observed_request,observed_cancel;
    reg [13:0] trigger_level;
    reg [1:0] mode;
    reg falling;
    reg [10:0] pre;
    reg [31:0] timeout_cycles;
    reg completed,frame_canceled,frame_triggered;
    reg [11:0] frame_first;
    reg [10:0] frame_trigger_index;
    // ADC-to-PS snapshots.  These are synchronized before being exposed
    // through the mailbox, so Linux never reads a torn multi-bit value.
    reg [31:0] mon_count_ps, mon_fault_ps, mon_duration_ps, mon_metric_ps, mon_baseline_ps;
    reg mon_baseline_valid_ps, mon_fault_active_ps;
    reg [31:0] mon_count_meta, mon_fault_meta, mon_duration_meta, mon_metric_meta, mon_baseline_meta;
    reg mon_valid_meta, mon_active_meta;
    // Continuous link monitor lives in the ADC clock domain.  It is enabled
    // by default for the candidate bitstream; its force pulse is gated by the
    // bridge so it only affects an actively armed capture request.
    reg monitor_arm;
    reg monitor_clear;
    reg monitor_started;
    reg monitor_enable_ps, monitor_rearm_toggle_ps, monitor_clear_toggle_ps;
    reg monitor_enable_meta, monitor_enable_adc;
    reg monitor_rearm_meta, monitor_rearm_adc, monitor_rearm_seen;
    reg monitor_clear_meta, monitor_clear_adc, monitor_clear_seen;
    wire monitor_baseline_valid, monitor_fault_active, monitor_event_start, monitor_event_end;
    wire [31:0] monitor_event_count, monitor_fault_windows, monitor_last_fault_windows;
    wire [31:0] monitor_last_metric, monitor_baseline_metric;
    wire bridge_busy,bridge_done,bridge_canceled,bridge_triggered;
    wire [11:0] bridge_first;
    wire [10:0] bridge_index;
    wire [11:0] source_index=frame_first+{1'b0,output_index};
    wire [27:0] source_pair;
    wire bridge_cancel=state==READ_CHECK && read_return==CANCEL_READ &&
        bram_dout==token && bram_dout==observed_cancel;
    adc_trigger_bridge bridge (
        .ps_clk(ps_clk),.ps_rst_n(ps_rst_n),.adc_clk(adc_clk),.adc_rst_n(adc_rst_n),
        .start(state==ARM),.cancel(bridge_cancel),.mode(mode),.level(trigger_level),
        .falling(falling),.pre_samples(pre),.auto_cycles(timeout_cycles),
        .busy(bridge_busy),.done(bridge_done),.canceled(bridge_canceled),
        .was_triggered(bridge_triggered),.first_index(bridge_first),.trigger_index(bridge_index),
        .valid(valid),.sample_first(sample_first),.sample_second(sample_second),
        .read_word(source_index[11:1]),.read_data(source_pair),
        .external_force(monitor_event_start)
    );

    adc_link_monitor #(
        .WINDOW_SAMPLES(1024),
        .CONFIRM_WINDOWS(2),
        .RECOVER_WINDOWS(2)
    ) link_monitor (
        .clk(adc_clk), .rst_n(adc_rst_n), .valid(valid),
        .enable(monitor_enable_adc), .arm(monitor_arm), .clear_stats(monitor_clear),
        .sample_first(sample_first), .sample_second(sample_second),
        .center(15'd8192), .low_ratio_q15(16'd19660),
        .recover_ratio_q15(16'd26214),
        .baseline_valid(monitor_baseline_valid),
        .fault_active(monitor_fault_active),
        .event_start(monitor_event_start), .event_end(monitor_event_end),
        .event_count(monitor_event_count),
        .fault_windows(monitor_fault_windows),
        .last_fault_windows(monitor_last_fault_windows),
        .last_metric(monitor_last_metric),
        .baseline_metric(monitor_baseline_metric)
    );

    // Release the monitor baseline once, after the ADC-domain reset.  This
    // pulse is independent of the slower PS mailbox state machine.
    always @(posedge adc_clk) begin
        if (!adc_rst_n) begin
            monitor_arm <= 1'b0;
            monitor_clear <= 1'b0;
            monitor_started <= 1'b0;
            monitor_enable_meta <= 1'b1;
            monitor_enable_adc <= 1'b1;
            monitor_rearm_meta <= 1'b0;
            monitor_rearm_adc <= 1'b0;
            monitor_rearm_seen <= 1'b0;
            monitor_clear_meta <= 1'b0;
            monitor_clear_adc <= 1'b0;
            monitor_clear_seen <= 1'b0;
        end else begin
            monitor_arm <= 1'b0;
            monitor_clear <= 1'b0;
            monitor_enable_meta <= monitor_enable_ps;
            monitor_enable_adc <= monitor_enable_meta;
            monitor_rearm_meta <= monitor_rearm_toggle_ps;
            monitor_rearm_adc <= monitor_rearm_meta;
            monitor_clear_meta <= monitor_clear_toggle_ps;
            monitor_clear_adc <= monitor_clear_meta;
            if (!monitor_started) begin
                monitor_arm <= 1'b1;
                monitor_started <= 1'b1;
            end else if (monitor_rearm_adc != monitor_rearm_seen) begin
                monitor_arm <= 1'b1;
                monitor_rearm_seen <= monitor_rearm_adc;
            end
            if (monitor_clear_adc != monitor_clear_seen) begin
                monitor_clear <= 1'b1;
                monitor_clear_seen <= monitor_clear_adc;
            end
        end
    end
    always @(posedge ps_clk) begin
        if(!ps_rst_n) begin
            mon_count_meta<=0; mon_fault_meta<=0; mon_duration_meta<=0;
            mon_metric_meta<=0; mon_baseline_meta<=0; mon_valid_meta<=0; mon_active_meta<=0;
            mon_count_ps<=0; mon_fault_ps<=0; mon_duration_ps<=0;
            mon_metric_ps<=0; mon_baseline_ps<=0; mon_baseline_valid_ps<=0; mon_fault_active_ps<=0;
        end else begin
            mon_count_meta<=monitor_event_count; mon_fault_meta<=monitor_fault_windows;
            mon_duration_meta<=monitor_last_fault_windows; mon_metric_meta<=monitor_last_metric;
            mon_baseline_meta<=monitor_baseline_metric; mon_valid_meta<=monitor_baseline_valid;
            mon_active_meta<=monitor_fault_active;
            mon_count_ps<=mon_count_meta; mon_fault_ps<=mon_fault_meta; mon_duration_ps<=mon_duration_meta;
            mon_metric_ps<=mon_metric_meta; mon_baseline_ps<=mon_baseline_meta;
            mon_baseline_valid_ps<=mon_valid_meta; mon_fault_active_ps<=mon_active_meta;
        end
    end
    always @* begin
        bram_addr=REQUEST; bram_din=0; bram_en=0; bram_we=0;
        case(state)
            INIT0: begin bram_addr=DONE; bram_en=1; bram_we=15; end
            INIT1: begin bram_addr=MAGIC; bram_din='h534e5031; bram_en=1; bram_we=15; end
            INIT2: begin bram_addr=COUNT; bram_din=2048; bram_en=1; bram_we=15; end
            INIT3: begin bram_addr=FEATURE; bram_din='h54524732; bram_en=1; bram_we=15; end
            INIT4: begin bram_addr=RESULT; bram_en=1; bram_we=15; end
            INIT5: begin bram_addr=TRIGGER_INDEX; bram_en=1; bram_we=15; end
            MON_CTRL_INIT: begin bram_addr=MON_CTRL; bram_din=1; bram_en=1; bram_we=15; end
            REQ_READ: bram_en=1;
            BUSY_WRITE: begin bram_addr=RESULT; bram_din=8; bram_en=1; bram_we=15; end
            CFG_READ: begin
                case(cfg_index)
                    0: bram_addr=LEVEL;
                    1: bram_addr=CTRL;
                    2: bram_addr=PRE;
                    default: bram_addr=TIMEOUT;
                endcase
                bram_en=1;
            end
            CANCEL_READ: begin bram_addr=CANCEL; bram_en=1; end
            SAMPLE_WRITE: begin
                bram_addr={19'd0,output_index,2'b00};
                bram_din={18'd0,source_index[0] ? source_pair[27:14] : source_pair[13:0]};
                bram_en=1; bram_we=15;
            end
            RESULT_WRITE: begin
                bram_addr=RESULT; bram_din=frame_canceled ? 4 : frame_triggered ? 3 : 1;
                bram_en=1; bram_we=15;
            end
            INDEX_WRITE: begin bram_addr=TRIGGER_INDEX; bram_din={21'd0,frame_trigger_index}; bram_en=1; bram_we=15; end
            DONE_WRITE: begin bram_addr=DONE; bram_din=token; bram_en=1; bram_we=15; end
            MON_STATUS_WRITE: begin bram_addr=MON_STATUS; bram_din={29'd0,monitor_enable_ps,mon_fault_active_ps,mon_baseline_valid_ps}; bram_en=1; bram_we=15; end
            MON_BASELINE_WRITE: begin bram_addr=MON_BASELINE; bram_din=mon_baseline_ps; bram_en=1; bram_we=15; end
            MON_METRIC_WRITE: begin bram_addr=MON_METRIC; bram_din=mon_metric_ps; bram_en=1; bram_we=15; end
            MON_COUNT_WRITE: begin bram_addr=MON_EVENT_COUNT; bram_din=mon_count_ps; bram_en=1; bram_we=15; end
            MON_FAULT_WRITE: begin bram_addr=MON_FAULT_WINDOWS; bram_din=mon_fault_ps; bram_en=1; bram_we=15; end
            MON_DURATION_WRITE: begin bram_addr=MON_LAST_DURATION; bram_din=mon_duration_ps; bram_en=1; bram_we=15; end
            MON_CTRL_READ: begin bram_addr=MON_CTRL; bram_en=1; end
            MON_CTRL_ACK: begin bram_addr=MON_CTRL; bram_din={31'd0,monitor_enable_ps}; bram_en=1; bram_we=15; end
            default: begin end
        endcase
    end
    always @(posedge ps_clk) begin
        if(!ps_rst_n) begin
            state<=INIT0; read_return<=REQ_READ; cfg_index<=0; output_index<=0;
            token<=0; observed_request<=0; observed_cancel<=0; trigger_level<=0;
            mode<=0; falling<=0; pre<=0; timeout_cycles<=1;
            completed<=0; frame_canceled<=0; frame_triggered<=0; frame_first<=0; frame_trigger_index<=0;
            monitor_enable_ps<=1'b1; monitor_rearm_toggle_ps<=1'b0; monitor_clear_toggle_ps<=1'b0;
        end else begin
            // Capture a one-cycle completion regardless of BRAM polling phase.
            if(bridge_done) begin
                completed<=1; frame_canceled<=bridge_canceled; frame_triggered<=bridge_triggered;
                frame_first<=bridge_first; frame_trigger_index<=bridge_index;
            end
            case(state)
                INIT0: state<=INIT1;
                INIT1: state<=INIT2;
                INIT2: state<=INIT3;
                INIT3: state<=INIT4;
                INIT4: state<=INIT5;
                INIT5: state<=MON_CTRL_INIT;
                MON_CTRL_INIT: state<=REQ_READ;
                REQ_READ: begin read_return<=REQ_READ; state<=READ_WAIT1; end
                CFG_READ: begin read_return<=CFG_READ; state<=READ_WAIT1; end
                CANCEL_READ: begin read_return<=CANCEL_READ; state<=READ_WAIT1; end
                MON_CTRL_READ: begin read_return<=MON_CTRL_READ; state<=READ_WAIT1; end
                READ_WAIT1: state<=READ_WAIT2;
                READ_WAIT2: state<=READ_CHECK;
                READ_CHECK: begin
                    if(read_return==REQ_READ) begin
                        observed_request<=bram_dout;
                        if(bram_dout!=0 && bram_dout!=token && bram_dout==observed_request) begin
                            token<=bram_dout; completed<=0; cfg_index<=0; output_index<=0;
                            observed_cancel<=0; state<=BUSY_WRITE;
                        end else state<=MON_CTRL_READ;
                    end else if(read_return==CFG_READ) begin
                        case(cfg_index)
                            0: trigger_level<=bram_dout[13:0];
                            1: begin mode<=bram_dout[1:0]; falling<=bram_dout[2]; end
                            2: pre<=bram_dout[10:0];
                            3: timeout_cycles<=bram_dout;
                            default: begin end
                        endcase
                        if(cfg_index==3) state<=ARM;
                        else begin cfg_index<=cfg_index+1'b1; state<=CFG_READ; end
                    end else if(read_return==MON_CTRL_READ) begin
                        monitor_enable_ps<=bram_dout[0];
                        if(bram_dout[1]) monitor_rearm_toggle_ps<=~monitor_rearm_toggle_ps;
                        if(bram_dout[2]) monitor_clear_toggle_ps<=~monitor_clear_toggle_ps;
                        state<=bram_dout[2:1]!=0 ? MON_CTRL_ACK : REQ_READ;
                    end else begin
                        observed_cancel<=bram_dout;
                        if(completed) state<=frame_canceled ? RESULT_WRITE : SAMPLE_READ;
                        else state<=CANCEL_READ;
                    end
                end
                BUSY_WRITE: state<=CFG_READ;
                ARM: state<=CANCEL_READ;
                SAMPLE_READ: state<=SAMPLE_WAIT1;
                SAMPLE_WAIT1: state<=SAMPLE_WAIT2;
                SAMPLE_WAIT2: state<=SAMPLE_WRITE;
                SAMPLE_WRITE: begin
                    if(output_index==2047) state<=RESULT_WRITE;
                    else begin output_index<=output_index+1'b1; state<=SAMPLE_READ; end
                end
                RESULT_WRITE: state<=INDEX_WRITE;
                INDEX_WRITE: state<=DONE_WRITE;
                DONE_WRITE: state<=MON_STATUS_WRITE;
                MON_STATUS_WRITE: state<=MON_BASELINE_WRITE;
                MON_BASELINE_WRITE: state<=MON_METRIC_WRITE;
                MON_METRIC_WRITE: state<=MON_COUNT_WRITE;
                MON_COUNT_WRITE: state<=MON_FAULT_WRITE;
                MON_FAULT_WRITE: state<=MON_DURATION_WRITE;
                MON_DURATION_WRITE: state<=REQ_READ;
                MON_CTRL_ACK: state<=REQ_READ;
                default: state<=INIT0;
            endcase
        end
    end
endmodule
