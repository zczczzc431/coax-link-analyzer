`timescale 1ns/1ps
// Candidate only. lane0 is the earlier raw sample in each pair.
module ddc_pair_fir725(
 input wire clk,rst_n,in_valid,in_frame_start,
 input wire [31:0] in_epoch,pair_step,phase_seed0,phase_seed1,
 input wire signed [13:0] in_sample0,in_sample1,
 input wire out_ready,output wire in_ready,out_valid,out_frame_start,
 output wire [31:0] out_epoch,output wire signed [43:0] out_i,out_q,
 output wire drop_bad,
 output wire [31:0] drop_count
);
 wire v0,v1,f0,f1;wire[31:0]e0,e1,p0,p1;
 wire signed[13:0]i0,q0,i1,q1;
 nco_mixer_dsp736 nco0(
  .clk(clk),.rst_n(rst_n),.in_valid(in_valid),.in_frame_start(in_frame_start),
  .in_epoch(in_epoch),.in_sample(in_sample0),.phase_inc(pair_step),.phase_seed(phase_seed0),
  .out_valid(v0),.out_frame_start(f0),.out_epoch(e0),.out_phase(p0),.out_i(i0),.out_q(q0));
 nco_mixer_dsp736 nco1(
  .clk(clk),.rst_n(rst_n),.in_valid(in_valid),.in_frame_start(in_frame_start),
  .in_epoch(in_epoch),.in_sample(in_sample1),.phase_inc(pair_step),.phase_seed(phase_seed1),
  .out_valid(v1),.out_frame_start(f1),.out_epoch(e1),.out_phase(p1),.out_i(i1),.out_q(q1));
 wire av,af;wire[31:0]ae;wire signed[19:0]ai,aq;
 r8_pair_fir725 r8(.clk(clk),.rst_n(rst_n),.in_valid(v0&&v1),
  .in_frame_start(f0),.in_epoch(e0),.in_i0(i0),.in_q0(q0),.in_i1(i1),.in_q1(q1),
  .out_valid(av),.out_frame_start(af),.out_epoch(ae),.out_i(ai),.out_q(aq));
 wire signed[26:0]bi,bq;wire[31:0]bdrop; wire r9_bad;
 cic2_stream_contract_candidate #(.IN_W(20),.ACC_W(27),.R(9)) r9(
  .clk(clk),.rst_n(rst_n),.in_valid(av),.in_frame_start(af),.in_epoch(ae),
  .in_i(ai),.in_q(aq),.out_ready(out_ready),.in_ready(),.out_valid(out_valid),
  .out_frame_start(out_frame_start),.out_epoch(out_epoch),.out_i(bi),.out_q(bq),.drop_count(bdrop),.drop_bad(r9_bad));
 assign out_i={{17{bi[26]}},bi};assign out_q={{17{bq[26]}},bq};
 reg mismatch;
 always @(posedge clk) begin
  if(!rst_n)mismatch<=0;
  else if((v0!=v1)||(v0&&v1&&((f0!=f1)||(e0!=e1))))mismatch<=1;
 end
 assign drop_count=bdrop | {32{mismatch}};
 assign drop_bad=r9_bad | mismatch;
 assign in_ready=1'b1;
endmodule
