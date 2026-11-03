`timescale 1ns/1ps
// Candidate-only fixed-point bridge from the 44-bit CIC accumulator domain to
// the legacy 18-bit signed I/Q sample contract.
//
// The R=8 x R=9, two-stage CIC has DC gain 8^2*9^2 = 5184.  With a 14-bit
// input the worst-case magnitude is 8191*5184 = 42,461,? < 2^26.  A
// symmetric round-to-nearest shift of 9 bits keeps the full-scale result
// inside the signed 18-bit range (about 82.9k), while shift 8 could overflow.
// The bridge is deliberately split into two registered stages.  Stage 1
// performs only signed round/shift; stage 2 performs the 18-bit saturation
// compare.  This keeps the 44-bit carry chain out of a single 5 ns path.
module cic_scale_sat_44_to18_candidate #(
    parameter integer IN_W = 44,
    parameter integer OUT_W = 18,
    parameter integer SHIFT = 9
) (
    input  logic clk, input logic rst_n,
    input  logic in_valid, input logic in_frame_start,
    input  logic [31:0] in_epoch,
    input  logic signed [IN_W-1:0] in_i, input logic signed [IN_W-1:0] in_q,
    input  logic out_ready,
    output logic in_ready,
    output logic out_valid, output logic out_frame_start,
    output logic [31:0] out_epoch,
    output logic signed [OUT_W-1:0] out_i, output logic signed [OUT_W-1:0] out_q,
    output logic drop_bad,
    output logic [31:0] drop_count,
    output logic out_clipped
);
    localparam integer EXT_W = IN_W+1;
    localparam integer MAX_OUT = (1<<(OUT_W-1))-1;
    localparam integer MIN_OUT = -(1<<(OUT_W-1));
    localparam integer ROUND = (SHIFT==0) ? 0 : (1<<(SHIFT-1));

    logic signed [EXT_W-1:0] norm_i, norm_q;
    logic v1, fs1;
    logic [31:0] epoch1;

    function automatic signed [EXT_W-1:0] round_shift(
        input logic signed [IN_W-1:0] x
    );
        logic signed [EXT_W-1:0] e;
        logic signed [EXT_W-1:0] mag;
        begin
            e = x;
            if (e >= 0) round_shift = (e + ROUND) >>> SHIFT;
            else begin mag = -e; round_shift = -((mag + ROUND) >>> SHIFT); end
        end
    endfunction

    assign in_ready = !(out_valid && !out_ready);

    always_ff @(posedge clk) begin : p
        logic clip_i, clip_q;
        if (!rst_n) begin
            v1 <= 1'b0; fs1 <= 1'b0; epoch1 <= '0; norm_i <= '0; norm_q <= '0;
            out_valid <= 1'b0; out_frame_start <= 1'b0; out_epoch <= '0;
            out_i <= '0; out_q <= '0; drop_count <= '0; drop_bad<=1'b0; out_clipped <= 1'b0;
        end else begin
            // Stage 2: saturate the registered rounded values.
            out_valid <= v1;
            out_frame_start <= v1 && fs1;
            out_epoch <= epoch1;
            out_clipped <= 1'b0;
            if (v1) begin
                clip_i = 1'b0; clip_q = 1'b0;
                if (norm_i > MAX_OUT) begin out_i <= MAX_OUT[OUT_W-1:0]; clip_i=1'b1; end
                else if (norm_i < MIN_OUT) begin out_i <= MIN_OUT[OUT_W-1:0]; clip_i=1'b1; end
                else out_i <= norm_i[OUT_W-1:0];
                if (norm_q > MAX_OUT) begin out_q <= MAX_OUT[OUT_W-1:0]; clip_q=1'b1; end
                else if (norm_q < MIN_OUT) begin out_q <= MIN_OUT[OUT_W-1:0]; clip_q=1'b1; end
                else out_q <= norm_q[OUT_W-1:0];
                out_clipped <= clip_i || clip_q;
            end
            // Stage 1: symmetric round-to-nearest and shift.
            v1 <= in_valid;
            fs1 <= in_valid && in_frame_start;
            epoch1 <= in_epoch;
            if (in_valid) begin norm_i <= round_shift(in_i); norm_q <= round_shift(in_q); end
            if (in_valid && out_valid && !out_ready) begin
                drop_count <= drop_count + 1'b1; drop_bad<=1'b1;
            end
        end
    end
endmodule
