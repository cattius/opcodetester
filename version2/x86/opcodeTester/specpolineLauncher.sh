#!/bin/bash

# instruction decode queue
decodedIcache=$(sudo perf stat -vv -e idq.dsb_uops : |& grep 'config ' | awk '{print $2}')
legacyDecode=$(sudo perf stat -vv -e idq.mite_uops : |& grep 'config ' | awk '{print $2}')
microcode=$(sudo perf stat -vv -e idq.ms_uops : |& grep 'config ' | awk '{print $2}')
uopsNotDelivered=$(sudo perf stat -vv -e idq_uops_not_delivered.core : |& grep 'config ' | awk '{print $2}')
idqEmpty=$(sudo perf stat -vv -e idq.empty : |& grep 'config ' | awk '{print $2}')

# bad speculation
uopsIssued=$(sudo perf stat -vv -e uops_issued.any : |& grep 'config ' | awk '{print $2}')
uopsRetired=$(sudo perf stat -vv -e uops_retired.retire_slots : |& grep 'config ' | awk '{print $2}')
recoveryCycles=$(sudo perf stat -vv -e int_misc.recovery_cycles : |& grep 'config ' | awk '{print $2}')
clocks=$(sudo perf stat -vv -e cpu_clk_unhalted.thread : |& grep 'config ' | awk '{print $2}')

# ports
port0=$(sudo perf stat -vv -e uops_dispatched_port.port_0 : |& grep 'config ' | awk '{print $2}')
port1=$(sudo perf stat -vv -e uops_dispatched_port.port_1 : |& grep 'config ' | awk '{print $2}')
port2=$(sudo perf stat -vv -e uops_dispatched_port.port_2 : |& grep 'config ' | awk '{print $2}')
port3=$(sudo perf stat -vv -e uops_dispatched_port.port_3 : |& grep 'config ' | awk '{print $2}')
port4=$(sudo perf stat -vv -e uops_dispatched_port.port_4 : |& grep 'config ' | awk '{print $2}')
port5=$(sudo perf stat -vv -e uops_dispatched_port.port_5 : |& grep 'config ' | awk '{print $2}')
port6=$(sudo perf stat -vv -e uops_dispatched_port.port_6 : |& grep 'config ' | awk '{print $2}')
port7=$(sudo perf stat -vv -e uops_dispatched_port.port_7 : |& grep 'config ' | awk '{print $2}')

#rest of pipeline
robStalls=$(sudo perf stat -vv -e resource_stalls.rob : |& grep 'config ' | awk '{print $2}')
rsStalls=$(sudo perf stat -vv -e resource_stalls.rs : |& grep 'config ' | awk '{print $2}')
lsd=$(sudo perf stat -vv -e lsd.uops : |& grep 'config ' | awk '{print $2}')
rsEmpty=$(sudo perf stat -vv -e rs_events.empty_cycles : |& grep 'config ' | awk '{print $2}')
retStalls=$(sudo perf stat -vv -e uops_retired.stall_cycles : |& grep 'config ' | awk '{print $2}')
l2Requests=$(sudo perf stat -vv -e l2_rqsts.references : |& grep 'config ' | awk '{print $2}')
l1dataReplacements=$(sudo perf stat -vv -e l1d.replacement : |& grep 'config ' | awk '{print $2}')
issueStalls=$(sudo perf stat -vv -e uops_issued.stall_cycles : |& grep 'config ' | awk '{print $2}')
ratStalls=$(sudo perf stat -vv -e int_misc.rat_stall_cycles : |& grep 'config ' | awk '{print $2}')
cyclesNoExecute=$(sudo perf stat -vv -e cycle_activity.cycles_no_execute : |& grep 'config ' | awk '{print $2}')

make
sudo ./specpoline $decodedIcache $legacyDecode $microcode $uopsNotDelivered $idqEmpty $uopsIssued $uopsRetired $recoveryCycles $clocks $port0 $port1 $port2 $port3 $port4 $port5 $port6 $port7 $robStalls $rsStalls $lsd $rsEmpty $retStalls $l2Requests $l1dataReplacements $issueStalls $ratStalls $cyclesNoExecute
