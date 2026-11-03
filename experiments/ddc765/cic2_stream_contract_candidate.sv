`timescale 1ns/1ps
// Candidate two-stage CIC. Input is continuous/no-backpressure; output is a
// one-cycle pulse and must be accepted when out_ready=1. A rejected output is
// counted explicitly and invalidates the downstream frame.
module cic2_stream_contract_candidate #(
  parameter integer IN_W=14, parameter integer ACC_W=44, parameter integer R=8
)(
  input logic clk,input logic rst_n,input logic in_valid,
  input logic in_frame_start,input logic [31:0] in_epoch,
  input logic signed [IN_W-1:0] in_i,input logic signed [IN_W-1:0] in_q,
  input logic out_ready,output logic in_ready,
  output logic out_valid,output logic out_frame_start,output logic [31:0] out_epoch,
  output logic signed [ACC_W-1:0] out_i,output logic signed [ACC_W-1:0] out_q,
  output logic drop_bad,
  output logic [31:0] drop_count
);
  localparam integer CW=(R<=2)?1:$clog2(R);
  logic [CW-1:0] decim_count;
  logic [31:0] active_epoch;
  logic frame_first_decim;
  logic signed [ACC_W-1:0] i0,i1,q0,q1;
  logic signed [ACC_W-1:0] ci0,ci1,cq0,cq1;
  logic signed [ACC_W-1:0] comb_sample_i,comb_sample_q,comb0_i,comb0_q;
  logic comb_sample_fs; logic [31:0] comb_sample_epoch;
  logic [1:0] comb_state;
  assign in_ready = 1'b1;
  initial begin if (R < 3) $fatal(1,"R must be >=3"); end

  always_ff @(posedge clk) begin : p
    logic signed [ACC_W-1:0] ni0,ni1,nq0,nq1,di1,dq1;
    logic [CW-1:0] next_count;
    logic decim_event;
    if (!rst_n) begin
      decim_count<='0; active_epoch<='0; frame_first_decim<=1'b0;
      i0<='0;i1<='0;q0<='0;q1<='0;ci0<='0;ci1<='0;cq0<='0;cq1<='0;
      comb_sample_i<='0;comb_sample_q<='0;comb0_i<='0;comb0_q<='0;comb_state<=0;
      comb_sample_fs<=1'b0; comb_sample_epoch<='0;
      out_valid<=1'b0;out_frame_start<=1'b0;out_epoch<='0;out_i<='0;out_q<='0;
      drop_count<='0; drop_bad<=1'b0;
    end else begin
      out_valid <= 1'b0;
      out_frame_start <= 1'b0;

      // Re-arm integrators atomically, but do not discard a comb result that
      // belongs to the previous frame; its marker travels with that sample.
      if (in_valid && in_frame_start) begin
        ni0 = $signed(in_i); nq0 = $signed(in_q);
        ni1 = ni0; nq1 = nq0;
        i0 <= ni0; i1 <= ni1; q0 <= nq0; q1 <= nq1;
        active_epoch <= in_epoch;
        decim_count <= {{(CW-1){1'b0}},1'b1};
        frame_first_decim <= 1'b1;
      end else if (in_valid) begin
        ni0 = i0 + $signed(in_i); nq0 = q0 + $signed(in_q);
        ni1 = i1 + ni0; nq1 = q1 + nq0;
        i0 <= ni0; i1 <= ni1; q0 <= nq0; q1 <= nq1;
        next_count = decim_count;
        decim_event = (next_count == R-1);
        if (decim_event) begin
          comb_sample_i <= ni1; comb_sample_q <= nq1;
          comb_sample_fs <= frame_first_decim;
          comb_sample_epoch <= active_epoch;
          comb_state <= 1;
          frame_first_decim <= 1'b0;
          decim_count <= '0;
        end else begin
          decim_count <= next_count + 1'b1;
        end
      end

      if (comb_state == 1) begin
        if (comb_sample_fs) begin
          comb0_i <= comb_sample_i; comb0_q <= comb_sample_q;
        end else begin
          comb0_i <= comb_sample_i - ci0; comb0_q <= comb_sample_q - cq0;
        end
        ci0 <= comb_sample_i; cq0 <= comb_sample_q;
        comb_state <= 2;
      end else if (comb_state == 2) begin
        if (comb_sample_fs) begin di1 = comb0_i; dq1 = comb0_q; end
        else begin di1 = comb0_i - ci1; dq1 = comb0_q - cq1; end
        ci1 <= comb0_i; cq1 <= comb0_q;
        if (out_ready) begin
          out_valid <= 1'b1; out_frame_start <= comb_sample_fs;
          out_epoch <= comb_sample_epoch; out_i <= di1; out_q <= dq1;
        end else begin
          drop_count <= drop_count + 1'b1; drop_bad<=1'b1;
        end
        comb_state <= 0;
      end
    end
  end
endmodule
