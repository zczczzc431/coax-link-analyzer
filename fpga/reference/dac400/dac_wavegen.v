`timescale 1ns/1ps
// Two-sample-per-clock 400 MSPS baseband source.
// The logic runs at 200 MHz and advances the phase accumulator twice per
// cycle. sample0/sample1 are serialized as I0/Q0/I1/Q1 by selectio_tx_2x.
module dac_wavegen (
    input wire clk_200m,
    input wire rst_n,
    input wire [31:0] tuning_word_async,
    input wire [31:0] control_word_async,
    output reg signed [15:0] sample0,
    output reg signed [15:0] sample1
);
    localparam [31:0] DEFAULT_STEP = 32'h06666666; // 10 MHz at 400 MSPS
    localparam [31:0] MIN_STEP = 32'd10737418;     // about 1 MHz
    localparam [31:0] MAX_STEP = 32'h40000000;     // 100 MHz
    localparam [31:0] NCO_MIN_FTW = 32'h04cccccd;  // 30 MHz at 1.6 GSPS
    localparam [31:0] NCO_MAX_FTW = 32'h28000000;  // 250 MHz at 1.6 GSPS

    reg [15:0] lut [0:1023];
    reg [255:0] square_lut [0:1023];
    reg [255:0] triangle_lut [0:1023];
    initial $readmemh("dac_sine_q15.mem", lut);
    initial $readmemh("dac_square_bandlimited_q15.mem", square_lut);
    initial $readmemh("dac_triangle_bandlimited_q15.mem", triangle_lut);

    (* ASYNC_REG = "TRUE" *) reg [31:0] tuning_meta, tuning_sync;
    (* ASYNC_REG = "TRUE" *) reg [31:0] control_meta, control_sync;
    reg commit_seen, commit_pending;
    reg [1:0] commit_wait;
    reg [31:0] phase, phase_step;
    reg [31:0] tuning_sync_d;
    reg [31:0] control_sync_d;
    reg tuning_valid_d;
    reg [3:0] shape_bank_candidate;
    reg [3:0] shape_bank_cfg;
    reg [15:0] amplitude;
    reg [1:0] mode;
    reg enabled;

    wire [31:0] phase_lane1 = phase + phase_step;
    wire [9:0] quarter_index0 = phase[30] ? ~phase[29:20] : phase[29:20];
    wire [9:0] quarter_index1 = phase_lane1[30] ? ~phase_lane1[29:20] : phase_lane1[29:20];

    // Proven 200 MSPS bank boundaries divided by two for f/400 MHz tuning.
    // ROM contents remain unchanged in this first interface-validation build.
    wire [3:0] shape_bank_pre = tuning_sync <= 32'h028f5c29 ? 4'd0 :
                                tuning_sync <= 32'h051eb852 ? 4'd1 :
                                tuning_sync <= 32'h07ae147b ? 4'd2 :
                                tuning_sync <= 32'h0a3d70a4 ? 4'd3 :
                                tuning_sync <= 32'h0f5c28f6 ? 4'd4 :
                                tuning_sync <= 32'h11eb851f ? 4'd5 :
                                tuning_sync <= 32'h13333333 ? 4'd6 :
                                tuning_sync <= 32'h13d70a3d ? 4'd7 : 4'd8;

    reg [15:0] sine_mag0, sine_mag1;
    reg sine_negative0, sine_negative1;
    reg [15:0] sine_mag0_d, sine_mag1_d;
    reg sine_negative0_d, sine_negative1_d;
    reg [255:0] triangle_word0, triangle_word1;
    reg [255:0] square_word0, square_word1;
    reg [3:0] shape_bank_d;
    reg signed [16:0] triangle0_d, triangle1_d;
    reg signed [16:0] square0_d, square1_d;
    reg [15:0] amplitude_d, amplitude_shape_d;
    reg [1:0] mode_d, mode_shape_d;
    reg enabled_d, enabled_shape_d;
    reg signed [16:0] raw0, raw1;
    (* KEEP = "TRUE", DONT_TOUCH = "TRUE" *) reg signed [16:0] raw0_pipe, raw1_pipe;
    reg signed [16:0] raw0_r, raw1_r;
    reg [15:0] amplitude_pipe, amplitude_r;
    reg enabled_pipe, enabled_r, enabled_product_r;
    reg signed [33:0] product0_r, product1_r;

    always @* begin
        case (mode_shape_d)
            2'd0: begin
                raw0 = sine_negative0_d ? -$signed({1'b0,sine_mag0_d}) : $signed({1'b0,sine_mag0_d});
                raw1 = sine_negative1_d ? -$signed({1'b0,sine_mag1_d}) : $signed({1'b0,sine_mag1_d});
            end
            2'd1: begin raw0 = square0_d; raw1 = square1_d; end
            2'd2: begin raw0 = triangle0_d; raw1 = triangle1_d; end
            2'd3: begin raw0 = 17'sd32767; raw1 = 17'sd32767; end
            default: begin raw0 = 17'sd0; raw1 = 17'sd0; end
        endcase
    end

    always @(posedge clk_200m) begin
        if (!rst_n) begin
            tuning_meta <= 0; tuning_sync <= 0; control_meta <= 0; control_sync <= 0;
            commit_seen <= 0; commit_pending <= 0; commit_wait <= 0;
            phase <= 0; phase_step <= DEFAULT_STEP;
            tuning_sync_d <= 0; control_sync_d <= 0; tuning_valid_d <= 0;
            shape_bank_candidate <= 0; shape_bank_cfg <= 0;
            amplitude <= 16'd16384; mode <= 0; enabled <= 0;
            sine_mag0 <= 0; sine_mag1 <= 0; sine_negative0 <= 0; sine_negative1 <= 0;
            sine_mag0_d <= 0; sine_mag1_d <= 0; sine_negative0_d <= 0; sine_negative1_d <= 0;
            triangle_word0 <= 0; triangle_word1 <= 0; square_word0 <= 0; square_word1 <= 0;
            shape_bank_d <= 0; triangle0_d <= 0; triangle1_d <= 0; square0_d <= 0; square1_d <= 0;
            amplitude_d <= 0; amplitude_shape_d <= 0;
            mode_d <= 0; mode_shape_d <= 0; enabled_d <= 0; enabled_shape_d <= 0;
            raw0_pipe <= 0; raw1_pipe <= 0; raw0_r <= 0; raw1_r <= 0;
            amplitude_pipe <= 0; amplitude_r <= 0;
            enabled_pipe <= 0; enabled_r <= 0; enabled_product_r <= 0;
            product0_r <= 0; product1_r <= 0; sample0 <= 0; sample1 <= 0;
        end else begin
            tuning_meta <= tuning_word_async;
            tuning_sync <= tuning_meta;
            control_meta <= control_word_async;
            control_sync <= control_meta;
            // Pipeline the slow control validation and waveform-bank decode.
            // The values are held stable by the PS commit protocol, so this
            // extra cycle removes wide compares from the 200 MHz datapath.
            tuning_sync_d <= tuning_sync;
            control_sync_d <= control_sync;
            tuning_valid_d <= ((control_sync[29:28] != 2'd3) &&
                               (tuning_sync >= MIN_STEP && tuning_sync <= MAX_STEP)) ||
                              ((control_sync[29:28] == 2'd3) &&
                               (tuning_sync >= NCO_MIN_FTW && tuning_sync <= NCO_MAX_FTW));
            shape_bank_candidate <= shape_bank_pre;
            if (!control_sync[31] && !commit_pending) begin
                commit_seen <= 0;
            end else if (control_sync[31] && !commit_seen && !commit_pending) begin
                commit_pending <= 1; commit_wait <= 2;
            end else if (commit_pending && commit_wait != 0) begin
                commit_wait <= commit_wait - 1'b1;
            end else if (commit_pending) begin
                commit_pending <= 0; commit_seen <= 1;
                if (tuning_valid_d) begin
                    if (control_sync_d[29:28] != 2'd3) begin
                        phase_step <= tuning_sync_d;
                        shape_bank_cfg <= shape_bank_candidate;
                    end
                    amplitude <= control_sync_d[15:0] > 16'd16384 ? 16'd16384 : control_sync_d[15:0];
                    mode <= control_sync_d[29:28]; enabled <= control_sync_d[30];
                end
            end

            phase <= phase + {phase_step[30:0],1'b0};
            sine_mag0 <= lut[quarter_index0]; sine_mag1 <= lut[quarter_index1];
            sine_negative0 <= phase[31]; sine_negative1 <= phase_lane1[31];
            sine_mag0_d <= sine_mag0; sine_mag1_d <= sine_mag1;
            sine_negative0_d <= sine_negative0; sine_negative1_d <= sine_negative1;
            triangle_word0 <= triangle_lut[phase[31:22]];
            triangle_word1 <= triangle_lut[phase_lane1[31:22]];
            square_word0 <= square_lut[phase[31:22]];
            square_word1 <= square_lut[phase_lane1[31:22]];
            shape_bank_d <= shape_bank_cfg;
            triangle0_d <= $signed(triangle_word0[shape_bank_d*16 +: 16]);
            triangle1_d <= $signed(triangle_word1[shape_bank_d*16 +: 16]);
            square0_d <= $signed(square_word0[shape_bank_d*16 +: 16]);
            square1_d <= $signed(square_word1[shape_bank_d*16 +: 16]);
            amplitude_d <= amplitude; mode_d <= mode; enabled_d <= enabled;
            amplitude_shape_d <= amplitude_d; mode_shape_d <= mode_d; enabled_shape_d <= enabled_d;
            raw0_pipe <= raw0; raw1_pipe <= raw1;
            amplitude_pipe <= amplitude_shape_d; enabled_pipe <= enabled_shape_d;
            raw0_r <= raw0_pipe; raw1_r <= raw1_pipe;
            amplitude_r <= amplitude_pipe; enabled_r <= enabled_pipe;
            product0_r <= raw0_r * $signed({1'b0,amplitude_r});
            product1_r <= raw1_r * $signed({1'b0,amplitude_r});
            enabled_product_r <= enabled_r;
            sample0 <= enabled_product_r ? product0_r >>> 15 : 16'sd0;
            sample1 <= enabled_product_r ? product1_r >>> 15 : 16'sd0;
        end
    end
endmodule
