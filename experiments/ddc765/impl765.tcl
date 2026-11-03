set_param general.maxThreads 2
set here [file dirname [file normalize [info script]]]
cd $here
set out physical765
if {[file exists $out]} {error "physical765_ALREADY_CONSUMED"}
file mkdir $out
foreach src {ddc764_split_integration.sv capture_split764.sv nco_mixer_dsp736.sv r8_pair_fir725.sv cic2_stream_contract_candidate.sv ddc_pair_fir725.sv cic_scale_sat_44_to18_candidate.sv comp_fir_complex_tdm_candidate.sv ddc736_zoom_chain_candidate.sv} {read_verilog -sv $src}
read_xdc clock764.xdc
synth_design -top ddc764_split_integration -part xc7z020clg484-2 -mode out_of_context -flatten_hierarchy rebuilt
opt_design
place_design
phys_opt_design
route_design
write_checkpoint $out/routed764.dcp
report_route_status -file $out/route764.rpt
report_utilization -hierarchical -file $out/util_route764.rpt
report_timing_summary -delay_type min_max -check_timing_verbose -report_unconstrained -max_paths 50 -file $out/timing_route764.rpt
report_timing -delay_type max -max_paths 100 -nworst 1 -file $out/max764.rpt
report_timing -delay_type min -max_paths 50 -nworst 1 -file $out/min764.rpt
report_drc -file $out/drc764.rpt
report_cdc -details -file $out/cdc764.rpt
report_exceptions -file $out/exceptions764.rpt
report_clock_interaction -file $out/clock_interaction764.rpt
exit

