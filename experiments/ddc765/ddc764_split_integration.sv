`timescale 1ns/1ps
// 764 integration candidate: real 736 zoom DDC output into split-domain capture.
module ddc764_split_integration #(parameter integer POINTS=16)(
 input logic clk200,rst200,clk30,rst30,
 input logic cmd_arm_30,cmd_cancel_30,cmd_release_30,
 input logic [31:0] ctl_epoch_30,ctl_owner_30,ctl_token_30,epoch_current_30,
 input logic rd_valid_30,input logic [$clog2(POINTS)-1:0] rd_addr_30,
 input logic [31:0] rd_epoch_30,rd_owner_30,rd_token_30,
 output logic rd_ready_30,rsp_valid_30,rsp_ok_30,output logic [35:0] rsp_data_30,input logic rsp_ready_30,
 output logic frame_ready_30,output logic [31:0] meta_epoch_30,meta_owner_30,meta_token_30,
 input logic raw_valid_200,raw_frame_start_200,input logic [31:0] raw_epoch_200,
 input logic signed[13:0] raw0_200,raw1_200,
 input logic [31:0] pair_step,phase_seed0,phase_seed1
);
 wire ddc_ready,ddc_valid,ddc_fs,ddc_clip,fir_clip,in_drop;
 wire [31:0] ddc_epoch,ddc_drops,scale_drops;
 wire signed[17:0] ddc_i,ddc_q;
 ddc736_zoom_chain_candidate #(.POINTS(POINTS)) zoom(
  .clk(clk200),.rst_n(!rst200),.in_valid(raw_valid_200),.in_frame_start(raw_frame_start_200),.in_epoch(raw_epoch_200),
  .pair_step(pair_step),.phase_seed0(phase_seed0),.phase_seed1(phase_seed1),.in_sample0(raw0_200),.in_sample1(raw1_200),
  .in_ready(ddc_ready),.ddc_drop_count(ddc_drops),.scale_drop_count(scale_drops),.ddc_clipped(ddc_clip),
  .out_valid(ddc_valid),.out_frame_start(ddc_fs),.out_epoch(ddc_epoch),.out_i(ddc_i),.out_q(ddc_q),.fir_clipped(fir_clip),.in_drop(in_drop));
 wire quality_bad = ddc_clip || fir_clip || in_drop || (ddc_drops!=0) || (scale_drops!=0);
 capture_split764 #(.POINTS(POINTS),.W(36),.WARMUP(0)) cap(
  .clk200(clk200),.rst200(rst200),.data_valid_200(ddc_valid),.data_i_200(ddc_i),.data_q_200(ddc_q),.quality_bad_200(quality_bad),.active_200(),.frozen_200(),
  .clk30(clk30),.rst30(rst30),.cmd_arm_30(cmd_arm_30),.cmd_cancel_30(cmd_cancel_30),.cmd_release_30(cmd_release_30),
  .ctl_epoch_30(ctl_epoch_30),.ctl_owner_30(ctl_owner_30),.ctl_token_30(ctl_token_30),.epoch_current_30(epoch_current_30),
  .rd_valid_30(rd_valid_30),.rd_addr_30(rd_addr_30),.rd_epoch_30(rd_epoch_30),.rd_owner_30(rd_owner_30),.rd_token_30(rd_token_30),
  .rd_ready_30(rd_ready_30),.rsp_valid_30(rsp_valid_30),.rsp_ok_30(rsp_ok_30),.rsp_data_30(rsp_data_30),.rsp_ready_30(rsp_ready_30),
  .frame_ready_30(frame_ready_30),.meta_epoch_30(meta_epoch_30),.meta_owner_30(meta_owner_30),.meta_token_30(meta_token_30),.error_30());
endmodule
