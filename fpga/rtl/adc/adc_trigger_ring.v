`timescale 1ns/1ps
// Candidate only: synchronous controls in the ADC domain. CDC/mailbox adapter
// must hold configuration stable and handshake arm/ready before integration.
// Two chronological 14-bit samples per 200 MHz cycle => 400 MSPS.
module adc_trigger_ring (
    input clk, input rst_n, input valid,
    input [13:0] sample_first, input [13:0] sample_second,
    input arm, input force_capture,
    input [13:0] level, input falling, input [10:0] pre_samples,
    output reg ready, output reg triggered,
    output reg [11:0] first_index,
    output reg [10:0] trigger_index,
    input read_clk, input [10:0] read_word,
    output reg [27:0] read_data
);
    // 4096 samples allow an odd-aligned 2048-sample frame without the final
    // pair overwriting its first sample. Do not reduce to 1024 pair words.
    (* ram_style="block" *) reg [27:0] memory [0:2047];
    reg [10:0] pointer;
    reg [11:0] history;
    reg [10:0] remaining, pre;
    reg [13:0] threshold;
    reg have_previous, previous_lt, previous_gt, down;
    reg [27:0] pair_pipe;
    reg valid_pipe, force_pipe;
    reg first_lt, first_gt, second_lt, second_gt;
    localparam IDLE=0, WAIT_EDGE=1, POST=2, FROZEN=3;
    reg [1:0] state;
    // Stage 1 ends all 14-bit comparisons at registers. Stage 2 consumes
    // the corresponding pair and four comparison bits together. Equality
    // has neither lt nor gt set, preserving the original inclusive crossing.
    always @(posedge clk) begin
        if(!rst_n || arm) begin
            pair_pipe<=0; valid_pipe<=0; force_pipe<=0;
            first_lt<=0; first_gt<=0; second_lt<=0; second_gt<=0;
        end else begin
            pair_pipe<={sample_second,sample_first};
            valid_pipe<=valid; force_pipe<=force_capture;
            first_lt<=sample_first<threshold; first_gt<=sample_first>threshold;
            second_lt<=sample_second<threshold; second_gt<=sample_second>threshold;
        end
    end
    wire cross_first=have_previous &&
        (down ? previous_gt && !first_gt : previous_lt && !first_lt);
    wire cross_second=down ? first_gt && !second_gt : first_lt && !second_lt;
    wire enough_first=history>={1'b0,pre};
    // history and all supported pre positions are even. Adding one cannot
    // cross an even threshold; avoid an unnecessary adder/comparator chain.
    wire enough_second=enough_first;
    wire hit_first=enough_first && (cross_first || force_pipe);
    wire hit_second=enough_second && cross_second;
    always @(posedge read_clk) read_data<=memory[read_word];
    always @(posedge clk) begin
        if(rst_n && !arm && valid_pipe && state!=FROZEN)
            memory[pointer]<=pair_pipe;
    end
    always @(posedge clk) begin
        if(!rst_n) begin
            pointer<=0; history<=0; remaining<=0; pre<=0; threshold<=0;
            previous_lt<=0; previous_gt<=0; have_previous<=0; down<=0; state<=IDLE;
            ready<=0; triggered<=0; first_index<=0; trigger_index<=0;
        end else if(arm) begin
            // Even positions 0..2044; odd requests round down. Reserve a
            // post-trigger pair so completion never needs a zero-count path.
            pre<=pre_samples[10:1]==10'h3ff ? 11'd2044 : {pre_samples[10:1],1'b0};
            threshold<=level; down<=falling; history<=0; have_previous<=0;
            ready<=0; triggered<=0; state<=WAIT_EDGE;
        end else if(state!=FROZEN) begin
            if(valid_pipe) begin
                pointer<=pointer+1'b1;
                previous_lt<=second_lt; previous_gt<=second_gt; have_previous<=1;
                if(history<2048) history<=history+12'd2;
                if(state==WAIT_EDGE && (hit_first || hit_second)) begin
                    first_index<={pointer,1'b0}+ (hit_first ? 12'd0 : 12'd1)-{1'b0,pre};
                    trigger_index<=pre;
                    triggered<=hit_first ? cross_first : 1'b1;
                    // For even pre, first lane requires 2046-pre more samples,
                    // second lane 2047-pre. No wide subtractor is necessary.
                    remaining<={~pre[10:1],!hit_first};
                    state<=POST;
                end else if(state==POST) begin
                    if(remaining<=2) begin ready<=1; state<=FROZEN; end
                    else remaining<=remaining-11'd2;
                end
            end else begin
                // A gap invalidates history and aborts the partial record:
                // never stitch separate bursts into one triggered frame.
                history<=0; have_previous<=0;
                if(state==POST) begin state<=WAIT_EDGE; triggered<=0; end
            end
        end
    end
endmodule
