`timescale 1ns/1ps
module tb_ddc764_split_integration_8192;
 localparam P=8192;localparam AW=13;localparam RAWN=594944;
 logic c200=0,c30=0;always #2.5 c200=~c200;always #16.666 c30=~c30;
 logic r200=1,r30=1,arm=0,cancel=0,rel=0,rdv=0,rspv,rok,ready,frame,rr=0;logic[31:0] ce=1,co=7,ct=9,ee=1,re=1,ro=7,rt=9;logic[AW-1:0]ra;logic[35:0]rd;logic[31:0]me,mo,mt;logic rawv=0,rawfs=0;logic signed[13:0]raw0=0,raw1=0;logic[31:0]raw_epoch=1;logic[31:0]step=32'h01000000,seed0=0,seed1=32'h01000000;reg signed[13:0]raw_mem[0:RAWN-1];integer idx=0;
 ddc764_split_integration #(.POINTS(P)) dut(.clk200(c200),.rst200(r200),.clk30(c30),.rst30(r30),.cmd_arm_30(arm),.cmd_cancel_30(cancel),.cmd_release_30(rel),.ctl_epoch_30(ce),.ctl_owner_30(co),.ctl_token_30(ct),.epoch_current_30(ee),.rd_valid_30(rdv),.rd_addr_30(ra),.rd_epoch_30(re),.rd_owner_30(ro),.rd_token_30(rt),.rd_ready_30(ready),.rsp_valid_30(rspv),.rsp_ok_30(rok),.rsp_data_30(rd),.rsp_ready_30(rr),.frame_ready_30(frame),.meta_epoch_30(me),.meta_owner_30(mo),.meta_token_30(mt),.raw_valid_200(rawv),.raw_frame_start_200(rawfs),.raw_epoch_200(raw_epoch),.raw0_200(raw0),.raw1_200(raw1),.pair_step(step),.phase_seed0(seed0),.phase_seed1(seed1));
 initial $readmemh("raw760.mem",raw_mem);
 always @(negedge c200) begin
   if(idx<RAWN) begin rawv<=1;rawfs<=(idx==0);raw0<=raw_mem[idx];raw1<=raw_mem[idx+1];idx<=idx+2;end else rawv<=0;
 end
 initial begin repeat(8)@(negedge c200);r200=0;r30=0;@(negedge c30);arm=1;@(negedge c30);arm=0;wait(frame);if(me!=1||mo!=7||mt!=9)$fatal(1,"DDC8192_META");@(negedge c30);ra=8191;rdv=1;@(negedge c30);rdv=0;wait(rspv);#0.1;if(!rok)$fatal(1,"DDC8192_READ_OK");rr=1;@(negedge c30);rr=0;cancel=1;@(negedge c30);cancel=0;if(frame)$fatal(1,"DDC8192_CANCEL");$display("DDC_SPLIT764_8192_PASS data=%h raw_idx=%0d",rd,idx);$finish;end
 initial begin #30000000;$fatal(1,"DDC_SPLIT764_8192_TIMEOUT raw_idx=%0d frame=%b",idx,frame);end
endmodule
