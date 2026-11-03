`timescale 1ns/1ps
// Candidate bridge. Both domains must be reset together, with synchronous
// deassertion in each domain. No reset of only one side of an active handshake.
// Config/result buses are bundled-data handshakes: source holds until ack.
// Integration must constrain their settling time; ASYNC_REG is not a bus proof.
module adc_trigger_bridge (
    input ps_clk, input ps_rst_n, input adc_clk, input adc_rst_n,
    input start, input cancel,
    input [1:0] mode, // 0=free/force, 1=normal, 2=auto, 3=single (no auto rearm)
    input [13:0] level, input falling, input [10:0] pre_samples,
    input [31:0] auto_cycles,
    output reg busy, output reg done, output reg canceled,
    output reg was_triggered, output reg [11:0] first_index,
    output reg [10:0] trigger_index,
    input valid, input [13:0] sample_first, input [13:0] sample_second,
    input [10:0] read_word, output [27:0] read_data,
    input external_force
);
    reg req_toggle, cancel_hold;
    reg [1:0] mode_hold;
    reg [13:0] level_hold;
    reg falling_hold;
    reg [10:0] pre_hold;
    reg [31:0] timeout_hold;
    reg ack_toggle, result_canceled, result_triggered;
    reg [11:0] result_first;
    reg [10:0] result_index;
    (* ASYNC_REG="TRUE" *) reg ack_sync1,ack_sync2;
    (* ASYNC_REG="TRUE" *) reg req_sync1,req_sync2;
    (* ASYNC_REG="TRUE" *) reg cancel_sync1,cancel_sync2;
    always @(posedge ps_clk) begin
        if(!ps_rst_n) begin
            req_toggle<=0; cancel_hold<=0; busy<=0; done<=0; canceled<=0;
            was_triggered<=0; first_index<=0; trigger_index<=0;
            mode_hold<=0; level_hold<=0; falling_hold<=0; pre_hold<=0; timeout_hold<=0;
            ack_sync1<=0; ack_sync2<=0;
        end else begin
            ack_sync1<=ack_toggle; ack_sync2<=ack_sync1; done<=0;
            if(!busy && start) begin
                mode_hold<=mode; level_hold<=level; falling_hold<=falling;
                // Preserve the TRG2 even-sample range across the bundled bus.
                pre_hold<=pre_samples[10:1]==10'h3ff ? 11'd2044 : {pre_samples[10:1],1'b0};
                // Precompute the zero-based terminal count in the slow domain.
                // Keep a 32-bit subtractor off the 200MHz comparator path.
                timeout_hold<=auto_cycles==0 ? 32'd0 : auto_cycles-32'd1;
                cancel_hold<=0; req_toggle<=!req_toggle; busy<=1; canceled<=0;
            end else if(busy) begin
                if(cancel) cancel_hold<=1;
                if(ack_sync2==req_toggle) begin
                    done<=1; busy<=0; cancel_hold<=0;
                    canceled<=result_canceled; was_triggered<=result_triggered;
                    first_index<=result_first; trigger_index<=result_index;
                end
            end
        end
    end
    reg req_seen,ring_arm,ring_cancel;
    reg [1:0] mode_adc;
    reg [13:0] level_adc;
    reg falling_adc;
    reg [10:0] pre_adc;
    reg [31:0] timeout_adc,elapsed;
    reg forced;
    localparam IDLE=0, WAIT_CLEAR=1, WAIT_READY=2;
    reg [1:0] state;
    wire ring_ready,ring_triggered;
    // External monitor events are accepted only while this PS request is
    // waiting for a frame.  A monitor event while the scope is idle cannot
    // leave stale data in the next manual acquisition.
    wire capture_force = forced || (external_force && state==WAIT_READY);
    wire [11:0] ring_first;
    wire [10:0] ring_index;
    // Cancellation resets only the ADC-domain ring, not handshake toggles.
    adc_trigger_ring ring (
        .clk(adc_clk),.rst_n(adc_rst_n && !ring_cancel),.valid(valid),
        .sample_first(sample_first),.sample_second(sample_second),
        .arm(ring_arm),.force_capture(capture_force),.level(level_adc),
        .falling(falling_adc),.pre_samples(pre_adc),.ready(ring_ready),
        .triggered(ring_triggered),.first_index(ring_first),.trigger_index(ring_index),
        .read_clk(ps_clk),.read_word(read_word),.read_data(read_data)
    );
    always @(posedge adc_clk) begin
        if(!adc_rst_n) begin
            req_sync1<=0; req_sync2<=0; cancel_sync1<=0; cancel_sync2<=0;
            req_seen<=0; ack_toggle<=0; state<=IDLE; ring_arm<=0; ring_cancel<=0;
            mode_adc<=0; level_adc<=0; falling_adc<=0; pre_adc<=0;
            timeout_adc<=0; elapsed<=0; forced<=0;
            result_canceled<=0; result_triggered<=0; result_first<=0; result_index<=0;
        end else begin
            req_sync1<=req_toggle; req_sync2<=req_sync1;
            cancel_sync1<=cancel_hold; cancel_sync2<=cancel_sync1;
            ring_arm<=0; ring_cancel<=0;
            case(state)
                IDLE: if(req_sync2!=req_seen) begin
                    req_seen<=req_sync2; mode_adc<=mode_hold; level_adc<=level_hold;
                    falling_adc<=falling_hold; pre_adc<=pre_hold; timeout_adc<=timeout_hold;
                    elapsed<=0; forced<=mode_hold==0; ring_arm<=1; state<=WAIT_CLEAR;
                end
                WAIT_CLEAR: begin
                    // Old frozen ready may still be high until ring consumes arm.
                    // Never mistake that stale ready for the new request's frame.
                    if(cancel_sync2) begin
                        ring_cancel<=1; forced<=0; result_canceled<=1;
                        result_triggered<=0; result_first<=0; result_index<=0;
                        ack_toggle<=req_seen; state<=IDLE;
                    end else if(!ring_ready && !ring_arm) state<=WAIT_READY;
                end
                WAIT_READY: begin
                    if(cancel_sync2) begin
                        ring_cancel<=1; forced<=0; result_canceled<=1;
                        result_triggered<=0; result_first<=0; result_index<=0;
                        ack_toggle<=req_seen; state<=IDLE;
                    end else if(ring_ready) begin
                        result_canceled<=0; result_triggered<=mode_adc!=0 && ring_triggered;
                        result_first<=ring_first; result_index<=ring_index;
                        ack_toggle<=req_seen; forced<=0; state<=IDLE;
                    end else if(mode_adc==2 && !forced) begin
                        if(elapsed>=timeout_adc) forced<=1;
                        else elapsed<=elapsed+1'b1;
                    end
                end
                default: state<=IDLE;
            endcase
        end
    end
endmodule
