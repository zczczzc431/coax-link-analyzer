`timescale 1ns/1ps
// Candidate-only, single-clock R=8 second-order CIC equivalent FIR.
// Lane 0 precedes lane 1. Each accepted pair advances the raw sequence by two.
// A frame starts on in_valid && in_frame_start; that pair is included in the
// cleared history. Select pair indices 3, 7, 11, ... (raw right edges 7, 15, ...).
// Input epoch must remain constant within a frame; upstream owns that check.
// There is no backpressure. Gaps advance neither history nor the pair counter.
// Five register banks: products, sum8, sum4, sum2, output. A selected pair at
// edge E produces out_valid/data after edge E+4. Restart flushes all old valids.
(* use_dsp = "no" *)
module r8_pair_fir725 (
    input  logic clk,
    input  logic rst_n,
    input  logic in_valid,
    input  logic in_frame_start,
    input  logic [31:0] in_epoch,
    input  logic signed [13:0] in_i0, in_q0, in_i1, in_q1,
    output logic out_valid,
    output logic out_frame_start,
    output logic [31:0] out_epoch,
    output logic signed [19:0] out_i, out_q
);
    // Nibble j is the coefficient for pair delay j, newest pair first.
    // a = [2,4,6,8,6,4,2,0], b = [1,3,5,7,7,5,3,1].
    localparam logic [31:0] COEFF_A = 32'h02468642;
    localparam logic [31:0] COEFF_B = 32'h13577531;

    // History entry 0 is the previously accepted pair, not the current pair.
    logic signed [13:0] hist_i0 [0:6];
    logic signed [13:0] hist_q0 [0:6];
    logic signed [13:0] hist_i1 [0:6];
    logic signed [13:0] hist_q1 [0:6];
    logic [1:0] pair_mod4;
    logic first_pending;

    logic signed [19:0] prod_i [0:15], prod_q [0:15];
    logic signed [19:0] sum8_i [0:7], sum8_q [0:7];
    logic signed [19:0] sum4_i [0:3], sum4_q [0:3];
    logic signed [19:0] sum2_i [0:1], sum2_q [0:1];
    logic prod_valid, prod_first;
    logic sum8_valid, sum8_first;
    logic sum4_valid, sum4_first;
    logic sum2_valid, sum2_first;
    logic [31:0] prod_epoch, sum8_epoch, sum4_epoch, sum2_epoch;

    // Extend before every shift/add. No data multiplication operator is used.
    // All coefficients are nonnegative and their total is 64. Every product
    // and every partial sum therefore fits signed 20 bits, including -524288.
    function automatic logic signed [19:0] weight (
        input logic signed [13:0] sample,
        input logic [3:0] coefficient
    );
        logic signed [19:0] extended_sample;
        begin
            extended_sample = {{6{sample[13]}}, sample};
            case (coefficient)
                4'd0: weight = 20'sd0;
                4'd1: weight = extended_sample;
                4'd2: weight = extended_sample <<< 1;
                4'd3: weight = (extended_sample <<< 1) + extended_sample;
                4'd4: weight = extended_sample <<< 2;
                4'd5: weight = (extended_sample <<< 2) + extended_sample;
                4'd6: weight = (extended_sample <<< 2) + (extended_sample <<< 1);
                4'd7: weight = (extended_sample <<< 3) - extended_sample;
                4'd8: weight = extended_sample <<< 3;
                default: weight = 20'sd0;
            endcase
        end
    endfunction

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            pair_mod4 <= 2'd0;
            first_pending <= 1'b0;
            for (integer h = 0; h < 7; h = h + 1) begin
                hist_i0[h] <= 14'sd0;
                hist_q0[h] <= 14'sd0;
                hist_i1[h] <= 14'sd0;
                hist_q1[h] <= 14'sd0;
            end
            for (integer p = 0; p < 16; p = p + 1) begin
                prod_i[p] <= 20'sd0;
                prod_q[p] <= 20'sd0;
            end
            for (integer s = 0; s < 8; s = s + 1) begin
                sum8_i[s] <= 20'sd0;
                sum8_q[s] <= 20'sd0;
            end
            for (integer s = 0; s < 4; s = s + 1) begin
                sum4_i[s] <= 20'sd0;
                sum4_q[s] <= 20'sd0;
            end
            for (integer s = 0; s < 2; s = s + 1) begin
                sum2_i[s] <= 20'sd0;
                sum2_q[s] <= 20'sd0;
            end
            prod_valid <= 1'b0;
            prod_first <= 1'b0;
            prod_epoch <= 32'd0;
            sum8_valid <= 1'b0;
            sum8_first <= 1'b0;
            sum8_epoch <= 32'd0;
            sum4_valid <= 1'b0;
            sum4_first <= 1'b0;
            sum4_epoch <= 32'd0;
            sum2_valid <= 1'b0;
            sum2_first <= 1'b0;
            sum2_epoch <= 32'd0;
            out_valid <= 1'b0;
            out_frame_start <= 1'b0;
            out_epoch <= 32'd0;
            out_i <= 20'sd0;
            out_q <= 20'sd0;
        end else begin
            // The tree advances every clock, including gaps in the input.
            // Its qualified metadata follows exactly the same four boundaries.
            for (integer s = 0; s < 8; s = s + 1) begin
                sum8_i[s] <= prod_i[2*s] + prod_i[2*s+1];
                sum8_q[s] <= prod_q[2*s] + prod_q[2*s+1];
            end
            for (integer s = 0; s < 4; s = s + 1) begin
                sum4_i[s] <= sum8_i[2*s] + sum8_i[2*s+1];
                sum4_q[s] <= sum8_q[2*s] + sum8_q[2*s+1];
            end
            for (integer s = 0; s < 2; s = s + 1) begin
                sum2_i[s] <= sum4_i[2*s] + sum4_i[2*s+1];
                sum2_q[s] <= sum4_q[2*s] + sum4_q[2*s+1];
            end
            out_i <= sum2_i[0] + sum2_i[1];
            out_q <= sum2_q[0] + sum2_q[1];
            sum8_valid <= prod_valid;
            sum8_first <= prod_valid && prod_first;
            sum8_epoch <= prod_epoch;
            sum4_valid <= sum8_valid;
            sum4_first <= sum8_valid && sum8_first;
            sum4_epoch <= sum8_epoch;
            sum2_valid <= sum4_valid;
            sum2_first <= sum4_valid && sum4_first;
            sum2_epoch <= sum4_epoch;
            out_valid <= sum2_valid;
            out_frame_start <= sum2_valid && sum2_first;
            out_epoch <= sum2_epoch;

            prod_valid <= 1'b0;
            prod_first <= 1'b0;
            if (in_valid) begin
                // Explicit current-pair bypass: nonblocking history writes
                // below must not delay the FIR window by one pair.
                prod_i[0] <= weight(in_i0, 4'd2);
                prod_q[0] <= weight(in_q0, 4'd2);
                prod_i[1] <= weight(in_i1, 4'd1);
                prod_q[1] <= weight(in_q1, 4'd1);
                for (integer d = 1; d < 8; d = d + 1) begin
                    if (in_frame_start) begin
                        prod_i[2*d] <= 20'sd0;
                        prod_q[2*d] <= 20'sd0;
                        prod_i[2*d+1] <= 20'sd0;
                        prod_q[2*d+1] <= 20'sd0;
                    end else begin
                        prod_i[2*d] <= weight(hist_i0[d-1], COEFF_A[4*d +: 4]);
                        prod_q[2*d] <= weight(hist_q0[d-1], COEFF_A[4*d +: 4]);
                        prod_i[2*d+1] <= weight(hist_i1[d-1], COEFF_B[4*d +: 4]);
                        prod_q[2*d+1] <= weight(hist_q1[d-1], COEFF_B[4*d +: 4]);
                    end
                end
                hist_i0[0] <= in_i0;
                hist_q0[0] <= in_q0;
                hist_i1[0] <= in_i1;
                hist_q1[0] <= in_q1;
                for (integer h = 1; h < 7; h = h + 1) begin
                    hist_i0[h] <= in_frame_start ? 14'sd0 : hist_i0[h-1];
                    hist_q0[h] <= in_frame_start ? 14'sd0 : hist_q0[h-1];
                    hist_i1[h] <= in_frame_start ? 14'sd0 : hist_i1[h-1];
                    hist_q1[h] <= in_frame_start ? 14'sd0 : hist_q1[h-1];
                end
                prod_epoch <= in_epoch;

                if (in_frame_start) begin
                    // This is pair 0 of the new frame; count it immediately.
                    pair_mod4 <= 2'd1;
                    first_pending <= 1'b1;
                    // Restart has priority over results from every old stage.
                    prod_valid <= 1'b0;
                    prod_first <= 1'b0;
                    sum8_valid <= 1'b0;
                    sum8_first <= 1'b0;
                    sum8_epoch <= 32'd0;
                    sum4_valid <= 1'b0;
                    sum4_first <= 1'b0;
                    sum4_epoch <= 32'd0;
                    sum2_valid <= 1'b0;
                    sum2_first <= 1'b0;
                    sum2_epoch <= 32'd0;
                    out_valid <= 1'b0;
                    out_frame_start <= 1'b0;
                    out_epoch <= 32'd0;
                end else begin
                    pair_mod4 <= pair_mod4 + 2'd1;
                    if (pair_mod4 == 2'd3) begin
                        prod_valid <= 1'b1;
                        prod_first <= first_pending;
                        first_pending <= 1'b0;
                    end
                end
            end
        end
    end
endmodule
