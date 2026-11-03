create_clock -name clk200 -period 5.000 [get_ports clk200]
create_clock -name clk30 -period 33.333 [get_ports clk30]
set_clock_groups -asynchronous -group [get_clocks clk200] -group [get_clocks clk30]
