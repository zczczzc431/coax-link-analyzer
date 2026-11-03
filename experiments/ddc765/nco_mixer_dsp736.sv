`timescale 1ns/1ps
// Candidate NCO multiplier using explicit DSP48E1 MREG/PREG stages.
// The phase/ROM contract is identical to nco_mixer_registered724; only the
// arithmetic primitive is changed to close the 5 ns coefficient->product path.
module nco_mixer_dsp736 #(
    parameter integer IN_W = 14, parameter integer OUT_W = 14,
    parameter integer PHASE_W = 32, parameter integer LUT_BITS = 12,
    parameter integer Q_BITS = 15,
    parameter string LUT_FILE = "sine_lut_q15.mem"
) (
    input logic clk,input logic rst_n,input logic in_valid,input logic in_frame_start,
    input logic [31:0] in_epoch,input logic signed [IN_W-1:0] in_sample,
    input logic [PHASE_W-1:0] phase_inc, input logic [PHASE_W-1:0] phase_seed,
    output logic out_valid,output logic out_frame_start,output logic [31:0] out_epoch,
    output logic [PHASE_W-1:0] out_phase,
    output logic signed [OUT_W-1:0] out_i,output logic signed [OUT_W-1:0] out_q
);
    localparam integer LUT_LEN=1<<LUT_BITS;
    localparam integer PROD_W=48;
    localparam integer MAX_OUT=(1<<(OUT_W-1))-1;
    localparam integer MIN_OUT=-(1<<(OUT_W-1));
    localparam integer HALF=1<<(Q_BITS-1);
    (* rom_style = "block" *) logic signed [15:0] sine_lut [0:LUT_LEN-1];
    logic [PHASE_W-1:0] phase;
    logic v0,fs0,v1,fs1,v2,fs2,v3,fs3;
    logic [31:0] epoch0,epoch1,epoch2,epoch3;
    logic [PHASE_W-1:0] phase0,phase1,phase2,phase3;
    logic [LUT_BITS-1:0] idx0,qidx0;
    logic signed [IN_W-1:0] sample0,sample1;
    logic signed [15:0] coeff_i1,coeff_q1;
    wire signed [29:0] a_mul = {{(30-IN_W){sample1[IN_W-1]}},sample1};
    wire signed [17:0] b_i_mul = {{2{coeff_i1[15]}},coeff_i1};
    wire signed [17:0] b_q_mul = {{2{coeff_q1[15]}},coeff_q1};
    wire signed [47:0] prod_i48,prod_q48;
    wire [6:0] opmode_mul=7'b0000101;
    wire [3:0] alumode_mul=4'b0000;
    wire [4:0] inmode_mul=5'b00000;
    wire [2:0] carryinsel_mul=3'b000;

    function automatic signed [OUT_W-1:0] sat_q15(input logic signed [PROD_W-1:0] p);
        logic signed [PROD_W:0] rounded,scaled;
        begin
            rounded = $signed(p) + HALF;
            scaled = rounded >>> Q_BITS;
            if (scaled > MAX_OUT) sat_q15 = MAX_OUT[OUT_W-1:0];
            else if (scaled < MIN_OUT) sat_q15 = MIN_OUT[OUT_W-1:0];
            else sat_q15 = scaled[OUT_W-1:0];
        end
    endfunction
    initial $readmemh(LUT_FILE,sine_lut);

    (* keep_hierarchy = "yes" *) DSP48E1 #(
      .A_INPUT("DIRECT"),.B_INPUT("DIRECT"),.USE_MULT("MULTIPLY"),
      .USE_SIMD("ONE48"),.AREG(0),.BREG(0),.ACASCREG(0),.BCASCREG(0),.MREG(1),.PREG(1)
    ) dsp_i (
      .A(a_mul),.B(b_i_mul),.C(48'd0),.D(25'd0),.P(prod_i48),
      .CLK(clk),.INMODE(inmode_mul),.OPMODE(opmode_mul),.ALUMODE(alumode_mul),
      .CARRYIN(1'b0),.CARRYINSEL(carryinsel_mul),.CARRYCASCIN(1'b0),
      .MULTSIGNIN(1'b0),.PCIN(48'd0),.ACIN(30'd0),.BCIN(18'd0),
      .CEA1(1'b0),.CEA2(1'b0),.CEB1(1'b0),.CEB2(1'b0),.CEC(1'b0),.CED(1'b0),
      .CEM(1'b1),.CEP(1'b1),.CEINMODE(1'b0),.CEALUMODE(1'b0),.CECTRL(1'b1),
      .RSTA(~rst_n),.RSTB(~rst_n),.RSTC(~rst_n),.RSTD(~rst_n),.RSTM(~rst_n),.RSTP(~rst_n),
      .RSTINMODE(~rst_n),.RSTALUMODE(~rst_n),.RSTCTRL(~rst_n),
      .ACOUT(),.BCOUT(),.CARRYOUT(),.CARRYCASCOUT(),.MULTSIGNOUT(),.PCOUT()
    );
    (* keep_hierarchy = "yes" *) DSP48E1 #(
      .A_INPUT("DIRECT"),.B_INPUT("DIRECT"),.USE_MULT("MULTIPLY"),
      .USE_SIMD("ONE48"),.AREG(0),.BREG(0),.ACASCREG(0),.BCASCREG(0),.MREG(1),.PREG(1)
    ) dsp_q (
      .A(a_mul),.B(b_q_mul),.C(48'd0),.D(25'd0),.P(prod_q48),
      .CLK(clk),.INMODE(inmode_mul),.OPMODE(opmode_mul),.ALUMODE(alumode_mul),
      .CARRYIN(1'b0),.CARRYINSEL(carryinsel_mul),.CARRYCASCIN(1'b0),
      .MULTSIGNIN(1'b0),.PCIN(48'd0),.ACIN(30'd0),.BCIN(18'd0),
      .CEA1(1'b0),.CEA2(1'b0),.CEB1(1'b0),.CEB2(1'b0),.CEC(1'b0),.CED(1'b0),
      .CEM(1'b1),.CEP(1'b1),.CEINMODE(1'b0),.CEALUMODE(1'b0),.CECTRL(1'b1),
      .RSTA(~rst_n),.RSTB(~rst_n),.RSTC(~rst_n),.RSTD(~rst_n),.RSTM(~rst_n),.RSTP(~rst_n),
      .RSTINMODE(~rst_n),.RSTALUMODE(~rst_n),.RSTCTRL(~rst_n),
      .ACOUT(),.BCOUT(),.CARRYOUT(),.CARRYCASCOUT(),.MULTSIGNOUT(),.PCOUT()
    );

    always_ff @(posedge clk) begin : p
        logic [PHASE_W-1:0] phase_used;
        logic [LUT_BITS-1:0] idx_used,qidx_used;
        if(!rst_n) begin
            phase<='0; v0<=0;fs0<=0;v1<=0;fs1<=0;v2<=0;fs2<=0;v3<=0;fs3<=0;
            epoch0<='0;epoch1<='0;epoch2<='0;epoch3<='0;
            phase0<='0;phase1<='0;phase2<='0;phase3<='0;
            idx0<='0;qidx0<='0;sample0<='0;sample1<='0;coeff_i1<='0;coeff_q1<='0;
            out_valid<=0;out_frame_start<=0;out_epoch<='0;out_phase<='0;out_i<='0;out_q<='0;
        end else begin
            // DSP48E1 MREG/PREG makes the product available one cycle after
            // the old three-stage metadata pipeline. Delay valid/metadata by
            // one stage so the first result is not zero and every sample stays
            // paired with its original phase/epoch.
            out_valid<=v3; out_frame_start<=v3&&fs3; out_epoch<=epoch3; out_phase<=phase3;
            if(v3) begin out_i<=sat_q15(prod_i48); out_q<=sat_q15(prod_q48); end
            v3<=v2;fs3<=fs2;epoch3<=epoch2;phase3<=phase2;
            v2<=v1;fs2<=fs1;epoch2<=epoch1;phase2<=phase1;
            v1<=v0;fs1<=fs0;epoch1<=epoch0;phase1<=phase0;
            if(v0) begin sample1<=sample0; coeff_i1<=sine_lut[idx0]; coeff_q1<=-$signed(sine_lut[qidx0]); end
            v0<=in_valid;fs0<=in_valid&&in_frame_start;epoch0<=in_epoch;
            if(in_valid) begin
                phase_used = in_frame_start ? phase_seed : phase;
                idx_used = phase_used[PHASE_W-1 -: LUT_BITS];
                qidx_used = idx_used + (1 << (LUT_BITS-2));
                sample0<=in_sample;idx0<=idx_used;qidx0<=qidx_used;phase0<=phase_used;
                phase<=phase_used+phase_inc;
            end
        end
    end
endmodule
