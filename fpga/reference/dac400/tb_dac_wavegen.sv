`timescale 1ns/1ps
module tb_dac_wavegen;
    reg clk=0, rst_n=0;
    reg [31:0] tuning=0, control=0;
    wire signed [15:0] sample0, sample1;
    integer cycle=0, last_rise=-1, rise_count=0, period_sum=0;
    reg prev_negative=0;
    dac_wavegen dut(.clk_200m(clk),.rst_n(rst_n),.tuning_word_async(tuning),
                    .control_word_async(control),.sample0(sample0),.sample1(sample1));
    always #2.5 clk=~clk;
    always @(posedge clk) cycle=cycle+1;
    task set_config(input [31:0] step,input [1:0] mode,input [15:0] amp,input en);
        begin
            @(negedge clk); tuning=step;
            repeat(5) @(negedge clk);
            control={1'b1,en,mode,12'd0,amp};
            repeat(15) @(negedge clk);
            if((mode!=3 && dut.phase_step!==step) || dut.mode!==mode || dut.enabled!==en)
                $fatal(1,"commit failed step=%h actual=%h mode=%d",step,dut.phase_step,dut.mode);
            if(dut.amplitude>16'd16384) $fatal(1,"new digital amplitude over cap");
            control={1'b0,en,mode,12'd0,amp};
            repeat(8) @(negedge clk);
        end
    endtask
    task check_period(input integer expected,input integer tolerance);
        integer n, period;
        begin
            repeat(45) @(negedge clk);
            rise_count=0; period_sum=0; last_rise=-1; prev_negative=sample0<0;
            for(n=0;n<300;n=n+1) begin
                @(negedge clk);
                if(prev_negative && sample0>=0) begin
                    if(last_rise>=0) begin
                        period=cycle-last_rise;
                        if(period<expected-tolerance || period>expected+tolerance)
                            $fatal(1,"period=%d expected=%d +/- %d",period,expected,tolerance);
                        period_sum=period_sum+period; rise_count=rise_count+1;
                    end
                    last_rise=cycle;
                end
                prev_negative=sample0<0;
            end
            if(rise_count<5) $fatal(1,"too few zero crossings");
            $display("PERIOD_PASS edges=%d mean_cycles=%f",rise_count,1.0*period_sum/rise_count);
        end
    endtask
    integer i, square_changes, triangle_distinct;
    reg signed [15:0] prev_sample;
    initial begin
        repeat(8) @(negedge clk); rst_n=1;
        if(dut.phase_step!==32'h06666666 || dut.amplitude!==16'd16384 || dut.enabled!==1'b0)
            $fatal(1,"reset must default to disabled and half-scale cap");
        repeat(80) begin
            @(negedge clk);
            if(sample0!==16'sd0 || sample1!==16'sd0) $fatal(1,"nonzero DAC sample before first enable commit");
        end
        set_config(32'h06666666,0,16'd10000,1); // 10 MHz at 400 MSPS
        check_period(20,0);
        set_config(32'h0ccccccd,0,16'd32767,1); // 20 MHz at 400 MSPS
        check_period(10,0);
        for(i=0;i<100;i=i+1) begin
            @(negedge clk);
            if(sample0>16'sd16384 || sample0< -16'sd16384 ||
               sample1>16'sd16384 || sample1< -16'sd16384) $fatal(1,"sine escaped amplitude cap");
        end
        set_config(32'h147ae148,1,16'd8000,1); // 32 MHz at 400 MSPS
        if(dut.shape_bank_cfg!==4'd8) $fatal(1,"32MHz square did not select highest pre-emphasis bank");
        square_changes=0; prev_sample=sample0;
        for(i=0;i<100;i=i+1) begin
            @(negedge clk);
            if(sample0!=prev_sample) square_changes=square_changes+1;
            if(sample0>16'sd8000 || sample0< -16'sd8000)
               $fatal(1,"band-limited square escaped amplitude cap: %d",sample0);
            prev_sample=sample0;
        end
        if(square_changes<70) $fatal(1,"band-limited square not varying smoothly");
        @(negedge clk); rst_n=0;
        repeat(8) @(negedge clk);
        rst_n=1;
        repeat(80) begin
            @(negedge clk);
            if(sample0!==16'sd0 || sample1!==16'sd0 || dut.enabled!==1'b0)
                $fatal(1,"PL reset during enabled output must silence DAC");
        end
        set_config(32'h147ae148,2,16'd8000,1); // 32 MHz at 400 MSPS
        if(dut.shape_bank_cfg!==4'd8) $fatal(1,"32MHz triangle did not select highest pre-emphasis bank");
        triangle_distinct=0; prev_sample=sample0;
        for(i=0;i<80;i=i+1) begin
            @(negedge clk);
            if(sample0!=prev_sample) triangle_distinct=triangle_distinct+1;
            prev_sample=sample0;
        end
        if(triangle_distinct<30) $fatal(1,"triangle not varying");
        set_config(32'h04cccccd,3,16'd6553,1); // 30MHz NCO lower bound, constant I
        repeat(12) @(negedge clk);
        for(i=0;i<40;i=i+1) begin
            @(negedge clk);
            if(sample0<16'sd6551 || sample0>16'sd6553 || sample1<16'sd6551 || sample1>16'sd6553)
                $fatal(1,"NCO constant-I sample changed: %d/%d",sample0,sample1);
        end
        set_config(32'h10000000,3,16'd6553,1); // 100MHz NCO FTW accepted
        repeat(12) @(negedge clk);
        if(sample0<16'sd6551 || sample0>16'sd6553 || sample1<16'sd6551 || sample1>16'sd6553)
            $fatal(1,"100MHz NCO constant-I sample invalid: %d/%d",sample0,sample1);
        set_config(32'h28000000,3,16'd8000,1); // 250MHz NCO FTW accepted
        repeat(12) @(negedge clk);
        if(sample0<16'sd7998 || sample0>16'sd8000 || sample1<16'sd7998 || sample1>16'sd8000)
            $fatal(1,"250MHz NCO constant-I sample invalid: %d/%d",sample0,sample1);
        set_config(32'h06666666,0,16'd8000,0);
        repeat(8) @(negedge clk);
        if(sample0!==16'sd0 || sample1!==16'sd0) $fatal(1,"disable did not zero data");
        $display("DAC_WAVEGEN_PASS default OFF, reset OFF, adaptive band-limited square/triangle through 32MHz, NCO constant-I 30/100/250MHz FTW, cap, disable");
        $finish;
    end
endmodule
