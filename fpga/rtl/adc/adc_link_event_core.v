`timescale 1ns/1ps
// Standalone event-capture candidate.
//
// Combines the continuous link monitor with the already verified trigger ring.
// It is intentionally a leaf module: integration can expose its status through
// the existing BRAM mailbox without changing the scope display path.
module adc_link_event_core #(
    parameter integer WINDOW_SAMPLES = 256,
    parameter integer CONFIRM_WINDOWS = 2,
    parameter integer RECOVER_WINDOWS = 2
) (
    input             clk,
    input             rst_n,
    input             valid,
    input             enable,
    input             arm,
    input      [13:0] sample_first,
    input      [13:0] sample_second,
    input      [14:0] center,
    input      [10:0] pre_samples,
    input      [15:0] low_ratio_q15,
    input      [15:0] recover_ratio_q15,

    output            baseline_valid,
    output            fault_active,
    output            event_start,
    output            event_end,
    output     [31:0] event_count,
    output     [31:0] fault_windows,
    output     [31:0] last_fault_windows,
    output     [31:0] last_metric,
    output     [31:0] baseline_metric,

    output            frame_ready,
    output            frame_triggered,
    output     [11:0] frame_first_index,
    output     [10:0] frame_trigger_index,
    input             read_clk,
    input      [10:0] read_word,
    output     [27:0] read_data
);
    wire monitor_start;

    adc_link_monitor #(
        .WINDOW_SAMPLES(WINDOW_SAMPLES),
        .CONFIRM_WINDOWS(CONFIRM_WINDOWS),
        .RECOVER_WINDOWS(RECOVER_WINDOWS)
    ) u_monitor (
        .clk(clk), .rst_n(rst_n), .valid(valid), .enable(enable), .arm(arm),
        .clear_stats(1'b0),
        .sample_first(sample_first), .sample_second(sample_second),
        .center(center), .low_ratio_q15(low_ratio_q15),
        .recover_ratio_q15(recover_ratio_q15),
        .baseline_valid(baseline_valid), .fault_active(fault_active),
        .event_start(monitor_start), .event_end(event_end),
        .event_count(event_count), .fault_windows(fault_windows),
        .last_fault_windows(last_fault_windows), .last_metric(last_metric),
        .baseline_metric(baseline_metric)
    );

    // Level is deliberately unreachable (falling at code zero) so the ring
    // is driven only by the monitor event. The existing ring still supplies
    // the verified circular storage, pre-trigger indexing and read port.
    adc_trigger_ring u_ring (
        .clk(clk), .rst_n(rst_n), .valid(valid),
        .sample_first(sample_first), .sample_second(sample_second),
        .arm(arm), .force_capture(monitor_start), .level(14'd0),
        .falling(1'b1), .pre_samples(pre_samples),
        .ready(frame_ready), .triggered(frame_triggered),
        .first_index(frame_first_index), .trigger_index(frame_trigger_index),
        .read_clk(read_clk), .read_word(read_word), .read_data(read_data)
    );
endmodule
