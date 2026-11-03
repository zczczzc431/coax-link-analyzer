`timescale 1ns/1ps
// Isolated zoom-chain integration: 725 real pair DDC -> 44-to-18 scale -> 735 TDM FIR.
// No production top/BD/PS/CDC is changed by this candidate.
module ddc736_zoom_chain_candidate #(
  parameter integer FIR_TAPS=47,
  parameter integer POINTS=256
)(
  input  logic clk,
  input  logic rst_n,
  input  logic in_valid,
  input  logic in_frame_start,
  input  logic [31:0] in_epoch,
  input  logic [31:0] pair_step,
  input  logic [31:0] phase_seed0,
  input  logic [31:0] phase_seed1,
  input  logic signed [13:0] in_sample0,
  input  logic signed [13:0] in_sample1,
  output logic in_ready,
  output logic ddc_drop_bad, scale_drop_bad,
  output logic [31:0] ddc_drop_count,
  output logic [31:0] scale_drop_count,
  output logic ddc_clipped,
  output logic out_valid,
  output logic out_frame_start,
  output logic [31:0] out_epoch,
  output logic signed [17:0] out_i,
  output logic signed [17:0] out_q,
  output logic fir_clipped,
  output logic in_drop
);
  wire ddc_valid, ddc_fs, ddc_ready;
  wire [31:0] ddc_epoch;
  wire signed [43:0] ddc_i, ddc_q;
  wire scale_valid, scale_fs, scale_ready;
  wire [31:0] scale_epoch;
  wire signed [17:0] scale_i, scale_q;
  wire fir_ready;
  wire [31:0] fir_drop;

  ddc_pair_fir725 ddc (
    .clk(clk), .rst_n(rst_n), .in_valid(in_valid), .in_frame_start(in_frame_start),
    .in_epoch(in_epoch), .pair_step(pair_step), .phase_seed0(phase_seed0), .phase_seed1(phase_seed1),
    .in_sample0(in_sample0), .in_sample1(in_sample1), .out_ready(scale_ready),
    .in_ready(ddc_ready), .out_valid(ddc_valid), .out_frame_start(ddc_fs), .out_epoch(ddc_epoch),
    .out_i(ddc_i), .out_q(ddc_q), .drop_count(ddc_drop_count), .drop_bad(ddc_drop_bad)
  );

  cic_scale_sat_44_to18_candidate scale (
    .clk(clk), .rst_n(rst_n), .in_valid(ddc_valid), .in_frame_start(ddc_fs), .in_epoch(ddc_epoch),
    .in_i(ddc_i), .in_q(ddc_q), .out_ready(fir_ready), .in_ready(scale_ready), .out_valid(scale_valid),
    .out_frame_start(scale_fs), .out_epoch(scale_epoch), .out_i(scale_i), .out_q(scale_q),
    .drop_count(scale_drop_count), .drop_bad(scale_drop_bad), .out_clipped(ddc_clipped)
  );

  comp_fir_complex_tdm_candidate #(.TAPS(FIR_TAPS), .COEF_FILE("comp_fir24_q18_19b.mem")) fir (
    .clk(clk), .rst_n(rst_n), .in_valid(scale_valid), .in_frame_start(scale_fs),
    .in_epoch(scale_epoch), .in_i(scale_i), .in_q(scale_q), .in_ready(fir_ready), .in_drop(in_drop), .out_clipped(fir_clipped),
    .out_valid(out_valid), .out_frame_start(out_frame_start), .out_epoch(out_epoch), .out_i(out_i), .out_q(out_q)
  );

  assign in_ready = ddc_ready;
endmodule
