`timescale 1ns/1ps
// Isolated low-rate complex compensation FIR with a 2-DSP time-multiplexed MAC.
// Input contract: one accepted CIC sample at most once per 36 clk cycles.
// 47-tap symmetric Q18 FIR: 24 independent coefficients, I and Q are interleaved
// through two registered multiplier/accumulator lanes. No production RTL is changed.
module comp_fir_complex_tdm_candidate #(
  parameter integer TAPS=47,
  parameter integer IN_W=18,
  parameter integer COEF_W=19,
  parameter integer ACC_W=48,
  parameter integer OUT_W=18,
  parameter integer COEF_FRAC=18,
  parameter integer MACS=24,
  parameter string COEF_FILE="comp_fir24_q18_19b.mem"
)(
  input  logic clk,
  input  logic rst_n,
  input  logic in_valid,
  input  logic in_frame_start,
  input  logic [31:0] in_epoch,
  input  logic signed [IN_W-1:0] in_i,
  input  logic signed [IN_W-1:0] in_q,
  output logic in_ready,
  output logic in_drop,
  output logic out_valid,
  output logic out_clipped,
  output logic out_frame_start,
  output logic [31:0] out_epoch,
  output logic signed [OUT_W-1:0] out_i,
  output logic signed [OUT_W-1:0] out_q
);
  localparam integer PROD_W=(IN_W+1)+COEF_W;
  localparam integer OUT_MAX=(1 << (OUT_W-1))-1;
  localparam integer OUT_MIN=-(1 << (OUT_W-1));
  localparam integer ROUND=(1 << (COEF_FRAC-1));

  logic signed [COEF_W-1:0] coef [0:MACS-1];
  logic signed [IN_W-1:0] delay_i [0:TAPS-1];
  logic signed [IN_W-1:0] delay_q [0:TAPS-1];
  logic signed [IN_W:0] pair_i [0:MACS-1];
  logic signed [IN_W:0] pair_q [0:MACS-1];
  logic signed [PROD_W-1:0] prod_i_q, prod_q_q;
  logic signed [IN_W:0] pair_i_sel_q, pair_q_sel_q;
  logic signed [COEF_W-1:0] coef_sel_q;
  logic signed [ACC_W-1:0] acc_i_q, acc_q_q;
  logic signed [ACC_W-1:0] final_i_q, final_q_q;
  logic signed [ACC_W-1:0] scaled_i_q, scaled_q_q;
  logic publish_pending_q;
  logic scaled_pending_q;
  logic [31:0] epoch_q;
  logic frame_start_q;
  logic busy_q, issue_done_q, prod_valid_q;
  logic pair_sel_valid_q, select_done_q;
  logic [5:0] select_idx_q;
  integer k;

  initial $readmemh(COEF_FILE, coef);

  assign in_ready = rst_n && !busy_q && !publish_pending_q && !scaled_pending_q;

  function automatic signed [IN_W:0] add_sample(
    input logic signed [IN_W-1:0] a,
    input logic signed [IN_W-1:0] b
  );
    logic signed [IN_W:0] aa, bb;
    begin
      aa = a;
      bb = b;
      add_sample = aa + bb;
    end
  endfunction

  function automatic signed [OUT_W-1:0] scale_sat(input logic signed [ACC_W-1:0] x);
    logic signed [ACC_W-1:0] y;
    begin
      if (x >= 0) y = (x + ROUND) >>> COEF_FRAC;
      else        y = (x + (ROUND-1)) >>> COEF_FRAC;
      if (y > OUT_MAX) scale_sat = OUT_MAX[OUT_W-1:0];
      else if (y < OUT_MIN) scale_sat = OUT_MIN[OUT_W-1:0];
      else scale_sat = y[OUT_W-1:0];
    end
  endfunction

  function automatic signed [OUT_W-1:0] sat_scaled(input logic signed [ACC_W-1:0] y);
    begin
      if (y > OUT_MAX) sat_scaled = OUT_MAX[OUT_W-1:0];
      else if (y < OUT_MIN) sat_scaled = OUT_MIN[OUT_W-1:0];
      else sat_scaled = y[OUT_W-1:0];
    end
  endfunction

  always_ff @(posedge clk) begin : p
    logic signed [ACC_W-1:0] final_i, final_q;
    if (!rst_n) begin
      in_drop <= 1'b0;
      out_valid <= 1'b0;
      out_clipped <= 1'b0;
      out_frame_start <= 1'b0;
      out_epoch <= '0;
      out_i <= '0;
      out_q <= '0;
      busy_q <= 1'b0;
      issue_done_q <= 1'b0;
      prod_valid_q <= 1'b0;
      pair_sel_valid_q <= 1'b0;
      select_done_q <= 1'b0;
      select_idx_q <= '0;
      prod_i_q <= '0;
      prod_q_q <= '0;
      pair_i_sel_q <= '0;
      pair_q_sel_q <= '0;
      coef_sel_q <= '0;
      acc_i_q <= '0;
      acc_q_q <= '0;
      final_i_q <= '0;
      final_q_q <= '0;
      scaled_i_q <= '0;
      scaled_q_q <= '0;
      publish_pending_q <= 1'b0;
      scaled_pending_q <= 1'b0;
      epoch_q <= '0;
      frame_start_q <= 1'b0;
      for (k=0; k<TAPS; k=k+1) begin
        delay_i[k] <= '0;
        delay_q[k] <= '0;
      end
      for (k=0; k<MACS; k=k+1) begin
        pair_i[k] <= '0;
        pair_q[k] <= '0;
      end
    end else begin
      in_drop <= in_valid && !in_ready;
      out_valid <= 1'b0;
      out_clipped <= 1'b0;
      out_frame_start <= 1'b0;

      if (scaled_pending_q) begin
        out_clipped <= (scaled_i_q > OUT_MAX || scaled_i_q < OUT_MIN || scaled_q_q > OUT_MAX || scaled_q_q < OUT_MIN);
        // Final comparison/mux is isolated from the wide add/shift stage.
        out_i <= sat_scaled(scaled_i_q);
        out_q <= sat_scaled(scaled_q_q);
        out_epoch <= epoch_q;
        out_frame_start <= frame_start_q;
        out_valid <= 1'b1;
        scaled_pending_q <= 1'b0;
      end else if (publish_pending_q) begin
        // Round/shift is deliberately a separate stage from the DSP/MAC.
        if (final_i_q >= 0) scaled_i_q <= (final_i_q + ROUND) >>> COEF_FRAC;
        else                scaled_i_q <= (final_i_q + (ROUND-1)) >>> COEF_FRAC;
        if (final_q_q >= 0) scaled_q_q <= (final_q_q + ROUND) >>> COEF_FRAC;
        else                scaled_q_q <= (final_q_q + (ROUND-1)) >>> COEF_FRAC;
        publish_pending_q <= 1'b0;
        scaled_pending_q <= 1'b1;
      end else if (!busy_q) begin
        if (in_valid) begin
          // Form the 24 symmetric sums before shifting the delay line.
          pair_i[0] <= add_sample(in_i, delay_i[TAPS-2]);
          pair_q[0] <= add_sample(in_q, delay_q[TAPS-2]);
          for (k=1; k<MACS-1; k=k+1) begin
            pair_i[k] <= add_sample(delay_i[k-1], delay_i[TAPS-2-k]);
            pair_q[k] <= add_sample(delay_q[k-1], delay_q[TAPS-2-k]);
          end
          pair_i[MACS-1] <= delay_i[(TAPS-1)/2-1];
          pair_q[MACS-1] <= delay_q[(TAPS-1)/2-1];
          for (k=TAPS-1; k>0; k=k-1) begin
            delay_i[k] <= delay_i[k-1];
            delay_q[k] <= delay_q[k-1];
          end
          delay_i[0] <= in_i;
          delay_q[0] <= in_q;
          epoch_q <= in_epoch;
          frame_start_q <= in_frame_start;
          busy_q <= 1'b1;
          issue_done_q <= 1'b0;
          prod_valid_q <= 1'b0;
          pair_sel_valid_q <= 1'b0;
          select_done_q <= 1'b0;
          select_idx_q <= '0;
          acc_i_q <= '0;
          acc_q_q <= '0;
        end
      end else if (!issue_done_q) begin
        // Register the pair-sum mux one cycle before the DSP input. This
        // removes the 24:1 dynamic mux from the 200 MHz multiplier path.
        if (pair_sel_valid_q) begin
          prod_i_q <= $signed(pair_i_sel_q) * $signed(coef_sel_q);
          prod_q_q <= $signed(pair_q_sel_q) * $signed(coef_sel_q);
          if (prod_valid_q) begin
            acc_i_q <= acc_i_q + prod_i_q;
            acc_q_q <= acc_q_q + prod_q_q;
          end
          prod_valid_q <= 1'b1;
          pair_sel_valid_q <= 1'b0;
          if (select_done_q) issue_done_q <= 1'b1;
        end
        if (!select_done_q) begin
          pair_i_sel_q <= pair_i[select_idx_q];
          pair_q_sel_q <= pair_q[select_idx_q];
          coef_sel_q <= coef[select_idx_q];
          pair_sel_valid_q <= 1'b1;
          if (select_idx_q == MACS-1) begin
            select_done_q <= 1'b1;
          end else begin
            select_idx_q <= select_idx_q + 1'b1;
          end
        end
      end else begin
        // Flush the last registered product and publish the completed sample.
        final_i = acc_i_q + prod_i_q;
        final_q = acc_q_q + prod_q_q;
        final_i_q <= final_i;
        final_q_q <= final_q;
        publish_pending_q <= 1'b1;
        busy_q <= 1'b0;
        issue_done_q <= 1'b0;
        prod_valid_q <= 1'b0;
      end
    end
  end
endmodule

