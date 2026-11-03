`timescale 1ns/1ps
//
// Continuous link monitor for the ADS5474 two-sample/200 MHz path.
//
// This is intentionally independent from the existing scope trigger path.
// It measures the absolute energy of a configurable-size sample window,
// learns a normal-state baseline, and reports a hysteretic intermittent-loss
// event.  The event_start pulse is suitable for arming a separate raw-frame
// capture block; no BRAM or PS interface is owned by this module.
//
// Samples are assumed to be unsigned offset-binary ADC codes.  The center
// input allows board-specific DC offset calibration (normally 14'h2000).
// Two chronological samples arrive on every valid clock.
module adc_link_monitor #(
    parameter integer WINDOW_SAMPLES = 256,
    parameter integer CONFIRM_WINDOWS = 2,
    parameter integer RECOVER_WINDOWS = 2,
    parameter integer BASELINE_SHIFT = 4
) (
    input             clk,
    input             rst_n,
    input             valid,
    input             enable,
    input             arm,
    input             clear_stats,
    input      [13:0] sample_first,
    input      [13:0] sample_second,
    input      [14:0] center,
    input      [15:0] low_ratio_q15,
    input      [15:0] recover_ratio_q15,

    output reg        baseline_valid,
    output reg        fault_active,
    output reg        event_start,
    output reg        event_end,
    output reg [31:0] event_count,
    output reg [31:0] fault_windows,
    output reg [31:0] last_fault_windows,
    output reg [31:0] last_metric,
    output reg [31:0] baseline_metric
);
    localparam integer PAIRS_PER_WINDOW = WINDOW_SAMPLES / 2;
    localparam integer PAIR_BITS = (PAIRS_PER_WINDOW <= 1) ? 1 : $clog2(PAIRS_PER_WINDOW);
    localparam integer CONFIRM_BITS = (CONFIRM_WINDOWS <= 1) ? 1 : $clog2(CONFIRM_WINDOWS + 1);
    localparam integer RECOVER_BITS = (RECOVER_WINDOWS <= 1) ? 1 : $clog2(RECOVER_WINDOWS + 1);

    reg [PAIR_BITS-1:0] pair_count;
    // 1024 samples x 14-bit full-scale absolute value fits in 24 bits
    // (8,388,? < 2^24), reducing the 200 MHz carry chain.
    reg [23:0] accum;
    reg [23:0] pair_metric_q, window_metric_q;
    reg pair_valid_q, window_ready_q;
    reg [CONFIRM_BITS-1:0] low_count;
    reg [RECOVER_BITS-1:0] recover_count;
    reg [31:0] low_limit_q, recover_limit_q;

    wire signed [14:0] first_delta  = $signed({1'b0, sample_first})  - $signed({1'b0, center});
    wire signed [14:0] second_delta = $signed({1'b0, sample_second}) - $signed({1'b0, center});
    wire [14:0] first_abs  = first_delta[14]  ? (~first_delta[14:0]  + 15'd1) : first_delta[14:0];
    wire [14:0] second_abs = second_delta[14] ? (~second_delta[14:0] + 15'd1) : second_delta[14:0];
    wire [23:0] pair_metric = {9'd0, first_abs} + {9'd0, second_abs};
    wire [23:0] metric_next = accum + pair_metric;

    // Ratios and limits are calculated only at the end of a window, so the
    // multipliers are not placed in the 200 MHz sample-to-sample path.
    wire [47:0] low_product = {16'd0, baseline_metric} * {32'd0, low_ratio_q15};
    wire [47:0] recover_product = {16'd0, baseline_metric} * {32'd0, recover_ratio_q15};
    always @(posedge clk) begin
        if (!rst_n) begin
            pair_count       <= 0;
            accum            <= 0;
            pair_metric_q    <= 0;
            window_metric_q  <= 0;
            pair_valid_q     <= 1'b0;
            window_ready_q   <= 1'b0;
            low_limit_q      <= 0;
            recover_limit_q  <= 0;
            baseline_valid   <= 1'b0;
            fault_active     <= 1'b0;
            event_start      <= 1'b0;
            event_end        <= 1'b0;
            event_count      <= 0;
            fault_windows    <= 0;
            last_fault_windows <= 0;
            last_metric      <= 0;
            baseline_metric  <= 0;
            low_count        <= 0;
            recover_count    <= 0;
        end else if (clear_stats) begin
            event_start        <= 1'b0;
            event_end          <= 1'b0;
            event_count        <= 0;
            last_fault_windows <= 0;
        end else begin
            event_start <= 1'b0;
            event_end   <= 1'b0;
            low_limit_q <= low_product[46:15];
            recover_limit_q <= recover_product[46:15];

            if (!enable || arm) begin
                pair_count     <= 0;
                accum          <= 0;
                pair_metric_q  <= 0;
                window_metric_q<= 0;
                pair_valid_q   <= 1'b0;
                window_ready_q <= 1'b0;
                low_count      <= 0;
                recover_count  <= 0;
                fault_active   <= 1'b0;
                fault_windows  <= 0;
                if (arm) baseline_valid <= 1'b0;
            end else begin
                // Process a completed window one cycle after the final pair.
                // The sample absolute-value and accumulator carry chains are
                // therefore separated by registers on the 200 MHz path.
                if (window_ready_q) begin
                    last_metric <= {8'd0, window_metric_q};
                    if (!baseline_valid) begin
                        baseline_metric <= {8'd0, window_metric_q};
                        baseline_valid  <= 1'b1;
                        low_count       <= 0;
                        recover_count   <= 0;
                    end else if (!fault_active) begin
                        // A low window must be confirmed before it becomes an
                        // event. The learned baseline remains fixed until the
                        // host requests a new arm/relearn operation.
                        recover_count   <= 0;
                        if (window_metric_q < low_limit_q) begin
                            if (low_count < CONFIRM_WINDOWS)
                                low_count <= low_count + 1'b1;
                            if (low_count + 1 >= CONFIRM_WINDOWS) begin
                                fault_active  <= 1'b1;
                                event_start   <= 1'b1;
                                event_count   <= event_count + 1'b1;
                                fault_windows <= 1;
                                low_count     <= 0;
                            end
                        end else begin
                            low_count <= 0;
                        end
                    end else begin
                        // Hold the baseline during a fault.  Recovery also
                        // uses hysteresis, preventing chatter near the edge.
                        if (window_metric_q > recover_limit_q) begin
                            if (recover_count < RECOVER_WINDOWS)
                                recover_count <= recover_count + 1'b1;
                            if (recover_count + 1 >= RECOVER_WINDOWS) begin
                                fault_active       <= 1'b0;
                                event_end          <= 1'b1;
                                last_fault_windows <= fault_windows;
                                fault_windows      <= 0;
                                recover_count      <= 0;
                            end
                        end else begin
                            recover_count <= 0;
                            if (fault_windows != 32'hffff_ffff)
                                fault_windows <= fault_windows + 1'b1;
                        end
                    end
                end else begin
                    window_ready_q <= 1'b0;
                end
                if (pair_valid_q) begin
                    if (pair_count == PAIRS_PER_WINDOW-1) begin
                        pair_count      <= 0;
                        window_metric_q <= accum + pair_metric_q;
                        accum           <= 0;
                        window_ready_q  <= 1'b1;
                    end else begin
                        pair_count <= pair_count + 1'b1;
                        accum      <= accum + pair_metric_q;
                    end
                end
                pair_valid_q <= valid;
                if (valid) pair_metric_q <= pair_metric;
            end
        end
    end
endmodule
