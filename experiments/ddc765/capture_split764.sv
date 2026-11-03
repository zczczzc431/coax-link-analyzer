`timescale 1ns/1ps
// 764 first split-domain capture candidate.
// 200 MHz: sample/write/freeze. 30 MHz: command, identity, cancel, Port-B read.
module capture_split764 #(parameter integer POINTS=16, W=36, WARMUP=0)(
 input logic clk200,rst200,
 input logic data_valid_200,input logic signed[W/2-1:0] data_i_200,data_q_200,input logic quality_bad_200,
 output logic active_200,output logic frozen_200,
 input logic clk30,rst30,
 input logic cmd_arm_30,cmd_cancel_30,cmd_release_30,
 input logic [31:0] ctl_epoch_30,ctl_owner_30,ctl_token_30,
 input logic [31:0] epoch_current_30,
 input logic rd_valid_30,input logic [$clog2(POINTS)-1:0] rd_addr_30,
 input logic [31:0] rd_epoch_30,rd_owner_30,rd_token_30,
 output logic rd_ready_30,output logic rsp_valid_30,rsp_ok_30,output logic [W-1:0] rsp_data_30,input logic rsp_ready_30,
 output logic frame_ready_30,output logic [31:0] meta_epoch_30,meta_owner_30,meta_token_30,
 output logic error_30
);
 localparam AW=$clog2(POINTS); localparam DW=W/2;
 logic [W-1:0] mem[0:POINTS-1];
 logic [AW:0] wr_count;
 logic [1:0] warm_q;
 logic [2:0] arm_wait200,frame_wait30;
 logic [31:0] arm_epoch_hold,arm_owner_hold,arm_token_hold;
 logic [31:0] arm_epoch1,arm_epoch2,arm_owner1,arm_owner2,arm_token1,arm_token2;
 (* ASYNC_REG = "TRUE" *) logic arm1_200,arm2_200; logic arm_tog30,arm_seen200;
 (* ASYNC_REG = "TRUE" *) logic cancel1_200,cancel2_200; logic cancel_tog30,cancel_seen200;
 (* ASYNC_REG = "TRUE" *) logic frame1_30,frame2_30; logic frame_tog200,frame_seen30;
 logic [31:0] meta_epoch_hold,meta_owner_hold,meta_token_hold;
 logic [31:0] meta_epoch1,meta_epoch2,meta_owner1,meta_owner2,meta_token1,meta_token2;
 logic rd_pending; logic [AW-1:0] rd_addr_q; logic rd_good_q;
 logic [31:0] epoch_q,owner_q,token_q;
 logic state_frozen200;
 assign active_200 = (arm_seen200==arm2_200) && !state_frozen200 && (wr_count!=0);
 assign frozen_200=state_frozen200;
 assign rd_ready_30=frame_ready_30 && !rd_pending && !rsp_valid_30;
 // 30 MHz holds arm tuple stable before its toggle is observed by 200 MHz.
 always_ff @(posedge clk30) begin
   if(rst30) begin
     arm_tog30<=0;cancel_tog30<=0;arm_epoch_hold<=0;arm_owner_hold<=0;arm_token_hold<=0;
     frame1_30<=0;frame2_30<=0;frame_seen30<=0;frame_wait30<=0;
     meta_epoch1<=0;meta_epoch2<=0;meta_owner1<=0;meta_owner2<=0;meta_token1<=0;meta_token2<=0;
     frame_ready_30<=0;meta_epoch_30<=0;meta_owner_30<=0;meta_token_30<=0;error_30<=0;
     rd_pending<=0;rsp_valid_30<=0;rsp_ok_30<=0;rsp_data_30<='0;rd_addr_q<=0;rd_good_q<=0;
     epoch_q<=0;owner_q<=0;token_q<=0;
   end else begin
     frame1_30<=frame_tog200;frame2_30<=frame1_30;
     meta_epoch1<=meta_epoch_hold;meta_epoch2<=meta_epoch1;meta_owner1<=meta_owner_hold;meta_owner2<=meta_owner1;meta_token1<=meta_token_hold;meta_token2<=meta_token1;
     rsp_valid_30<=0;
     if(cmd_arm_30 && !frame_ready_30) begin
       arm_epoch_hold<=ctl_epoch_30;arm_owner_hold<=ctl_owner_30;arm_token_hold<=ctl_token_30;arm_tog30<=~arm_tog30;error_30<=0;
     end
     if(cmd_cancel_30) begin
       cancel_tog30<=~cancel_tog30;frame_ready_30<=0;rd_pending<=0;rsp_valid_30<=0;error_30<=1;
     end else begin
       if(frame2_30!=frame_seen30 && !frame_ready_30) begin
         frame_seen30<=frame2_30;frame_wait30<=2;meta_epoch_30<=meta_epoch2;meta_owner_30<=meta_owner2;meta_token_30<=meta_token2;
       end else if(frame_wait30!=0) frame_wait30<=frame_wait30-1'b1;
       else if(frame2_30==frame_seen30 && frame_wait30==0 && meta_epoch_30!=0 && !error_30) frame_ready_30<=1;
     end
     if(cmd_release_30 && frame_ready_30 && !rd_pending && !rsp_valid_30) begin frame_ready_30<=0;meta_epoch_30<=0;meta_owner_30<=0;meta_token_30<=0;end
     if(rd_valid_30 && rd_ready_30) begin
       rd_pending<=1;rd_addr_q<=rd_addr_30;
       rd_good_q=(rd_epoch_30!=0)&&(rd_owner_30!=0)&&(rd_token_30!=0)&&rd_epoch_30==meta_epoch_30&&rd_owner_30==meta_owner_30&&rd_token_30==meta_token_30&&(rd_addr_30<POINTS);
     end
     if(rd_pending) begin rd_pending<=0;rsp_valid_30<=1;rsp_ok_30<=rd_good_q;rsp_data_30<=rd_good_q?mem[rd_addr_q]:'0;end
     if(rsp_valid_30 && rsp_ready_30) rsp_valid_30<=0;
   end
 end
 // 200 MHz receives the ARM/cancel toggles after bundled tuple settling.
 always_ff @(posedge clk200) begin
   if(rst200) begin
     arm1_200<=0;arm2_200<=0;arm_seen200<=0;cancel1_200<=0;cancel2_200<=0;cancel_seen200<=0;
     arm_epoch1<=0;arm_epoch2<=0;arm_owner1<=0;arm_owner2<=0;arm_token1<=0;arm_token2<=0;arm_wait200<=0;
     wr_count<=0;warm_q<=0;state_frozen200<=0;frame_tog200<=0;meta_epoch_hold<=0;meta_owner_hold<=0;meta_token_hold<=0;
   end else begin
     arm1_200<=arm_tog30;arm2_200<=arm1_200;cancel1_200<=cancel_tog30;cancel2_200<=cancel1_200;
     arm_epoch1<=arm_epoch_hold;arm_epoch2<=arm_epoch1;arm_owner1<=arm_owner_hold;arm_owner2<=arm_owner1;arm_token1<=arm_token_hold;arm_token2<=arm_token1;
     if(arm2_200!=arm_seen200) begin arm_seen200<=arm2_200;arm_wait200<=2;end
     else if(arm_wait200!=0) arm_wait200<=arm_wait200-1'b1;
     if(cancel2_200!=cancel_seen200) begin cancel_seen200<=cancel2_200;state_frozen200<=0;wr_count<=0;end
     if(arm_wait200==0 && arm_seen200==arm2_200 && !state_frozen200 && arm_seen200!=0 && wr_count==0) begin
       meta_epoch_hold<=arm_epoch2;meta_owner_hold<=arm_owner2;meta_token_hold<=arm_token2;warm_q<=WARMUP;wr_count<=1; // arm marker
     end
     if(!state_frozen200 && wr_count!=0 && data_valid_200) begin
       if(quality_bad_200) begin state_frozen200<=0;wr_count<=0; end
       else if(warm_q!=0) warm_q<=warm_q-1'b1;
       else begin mem[wr_count-1]<= {data_i_200,data_q_200}; if(wr_count==POINTS) begin state_frozen200<=1;frame_tog200<=~frame_tog200;end else wr_count<=wr_count+1'b1; end
     end
   end
 end
endmodule


