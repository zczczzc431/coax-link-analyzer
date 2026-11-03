`timescale 1ns / 1ps
// Single-clock, single-frame mailbox. Port A (PS) owns REQUEST; Port B (PL)
// owns samples/DONE/MAGIC/COUNT. Never write REQUEST from PL. Native addresses
// are byte offsets into a 16 KiB BRAM. Read latency may be one or two cycles.
module adc_frame_snapshot (
    input clk,
    input rst_n,
    input [13:0] sample,
    input valid,
    input [31:0] bram_dout,
    output reg [31:0] bram_addr,
    output reg [31:0] bram_din,
    output reg bram_en,
    output reg [3:0] bram_we
);
    localparam [31:0] REQUEST = 32'h2000, DONE = 32'h2004,
                      MAGIC = 32'h2008, COUNT = 32'h200c;
    localparam [31:0] ABI_MAGIC = 32'h534e5031; // SNP1
    localparam [3:0] INIT_DONE=0, INIT_MAGIC=1, INIT_COUNT=2,
        POLL_READ=3, POLL_WAIT1=4, POLL_WAIT2=5, POLL_CHECK=6,
        WAIT_GAP=7, WAIT_FIRST=8, CAPTURE=9, PUBLISH=10;
    reg [3:0] state;
    reg [10:0] index;
    reg [31:0] token;
    reg [31:0] observed_request;

    always @* begin
        bram_addr = REQUEST;
        bram_din = 0;
        bram_en = 0;
        bram_we = 0;
        begin
            case (state)
                INIT_DONE: begin bram_addr=DONE; bram_en=1; bram_we=15; end
                INIT_MAGIC: begin bram_addr=MAGIC; bram_din=ABI_MAGIC; bram_en=1; bram_we=15; end
                INIT_COUNT: begin bram_addr=COUNT; bram_din=2048; bram_en=1; bram_we=15; end
                POLL_READ: bram_en=1;
                WAIT_FIRST, CAPTURE: begin
                    bram_addr={19'd0,index,2'b00};
                    bram_din={18'd0,sample};
                    bram_en=valid;
                    bram_we=valid ? 4'hf : 4'h0;
                end
                PUBLISH: begin bram_addr=DONE; bram_din=token; bram_en=1; bram_we=15; end
                default: begin end
            endcase
        end
    end

    // Synchronous reset keeps RAMB address/control inputs synchronous.
    always @(posedge clk) begin
        if (!rst_n) begin
            state <= INIT_DONE;
            index <= 0;
            token <= 0;
            observed_request <= 0;
        end else begin
            case (state)
                INIT_DONE: state <= INIT_MAGIC;
                INIT_MAGIC: state <= INIT_COUNT;
                INIT_COUNT: state <= POLL_READ;
                POLL_READ: state <= POLL_WAIT1;
                POLL_WAIT1: state <= POLL_WAIT2;
                POLL_WAIT2: state <= POLL_CHECK;
                POLL_CHECK: begin
                    // Two identical polls reject a transient caused by an
                    // asynchronous PS write colliding with a Port B read.
                    observed_request <= bram_dout;
                    if (bram_dout != 0 && bram_dout != token && bram_dout == observed_request) begin
                        token <= bram_dout;
                        index <= 0;
                        state <= WAIT_GAP;
                    end else state <= POLL_READ;
                end
                // Discard any burst already underway; start on the next burst.
                WAIT_GAP: if (!valid) state <= WAIT_FIRST;
                WAIT_FIRST: if (valid) begin index <= 1; state <= CAPTURE; end
                CAPTURE: if (valid) begin
                    if (index == 2047) state <= PUBLISH;
                    else index <= index + 1'b1;
                end else begin
                    // A short/aborted FIFO burst must not be spliced into the
                    // following frame. Overwrite its partial data from index 0.
                    index <= 0;
                    state <= WAIT_FIRST;
                end
                // The final sample write precedes DONE publication by one cycle.
                PUBLISH: state <= POLL_READ;
                default: state <= INIT_DONE;
            endcase
        end
    end
endmodule
