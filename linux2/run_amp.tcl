# run_amp.tcl - one-shot on-board bring-up for the ZYNQ7010 dual-core AMP demo
#
# Usage (from anywhere):
#     xsct run_amp.tcl
#
# What it does:
#   1. reset the PS
#   2. program amp_system/_ide/bitstream/Sys_wrapper.bit
#   3. run amp_system/_ide/psinit/ps7_init.tcl (ps7_init + ps7_post_config)
#   4. download hello_world.elf  to ARM Cortex-A9 MPCore #0
#      download hello_world_1.elf to ARM Cortex-A9 MPCore #1
#   5. continue both cores
#
# WARNING: step 1 resets the PS, so DDR contents and any running software
#          on the board are lost.

set ws [file dirname [file normalize [info script]]]
set bit    [file join $ws amp_system _ide bitstream Sys_wrapper.bit]
set psinit [file join $ws amp_system _ide psinit ps7_init.tcl]
set elf0   [file join $ws hello_world build hello_world.elf]
set elf1   [file join $ws hello_world_1 build hello_world_1.elf]

foreach f [list $bit $psinit $elf0 $elf1] {
    if {![file exists $f]} {
        puts "ERROR: missing file: $f"
        exit 1
    }
}

connect

puts "== \[1/5\] reset PS =="
targets -set -filter {name =~ "APU*"}
rst -system
after 1000

puts "== \[2/5\] program PL =="
fpga -file $bit

puts "== \[3/5\] PS init (ps7_init) =="
source $psinit
ps7_init
ps7_post_config

puts "== \[4/5\] download ELFs =="
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
if {[catch {rst -processor} msg]} { puts "   (CPU0 rst -processor: $msg)" }
dow $elf0
puts "   CPU0 <- hello_world.elf"

targets -set -filter {name =~ "ARM Cortex-A9 MPCore #1"}
if {[catch {rst -processor} msg]} { puts "   (CPU1 rst -processor: $msg)" }
dow $elf1
puts "   CPU1 <- hello_world_1.elf"

puts "== \[5/5\] run =="
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #0"}
con
targets -set -filter {name =~ "ARM Cortex-A9 MPCore #1"}
con

puts "RUNNING"
exit 0
