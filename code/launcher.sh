#!/bin/bash

if [[ "$8" = "" ]]; then
  echo "usage: ./launcher.sh sandsifterLogFile outputFile portAnalysisOn instrLog testValid testInvalid testUndocumented testValidOtherModes"
  exit 0
fi

echo "Note: this launcher requires sudo so we can load the kernel driver and use the perf tool (for performance counter monitoring)."

#get CPU info
arch=$(lscpu | grep "Architecture: " | awk '{print $2}')
vendor=$(lscpu | grep "Vendor ID: " | awk '{print $3}')
if [[ "$arch" != "x86_64" || "$vendor" != "GenuineIntel" ]]; then
  echo $arch
  echo $vendor
  echo "Your system is unsupported."
  echo "This program only supports modern Intel processors running x86_64 Linux."
  echo "In order to use this program, you will need to adapt the source code for your system."
  exit 0
fi
#This is a hack to get the microarchitecture, will only work on modern Intel Core i3/5/7 processors. Can't find an actual reliable way to get the microarch info.
modelName=$(lscpu | sed -n 's/Model name:          //p')
echo "Detected CPU model: $modelName."
versionNum=$(lscpu | grep "Model name: " | awk '{print substr($5, 4, 1)}')

if [[ "$versionNum" == "2" ]]; then
  microarch="Sandybridge"
elif [[ "$versionNum" == "3" ]]; then
  microarch="Ivybridge"
elif [[ "$versionNum" == "4" ]]; then
  microarch="Haswell"
elif [[ "$versionNum" == "5" ]]; then
  microarch="Broadwell"
elif [[ "$versionNum" == "6" ]]; then
  microarch="Skylake"
else
  echo "Could not automatically identify your CPU microarchitecture."
  echo "The following microarchitectures are supported: Sandybridge, Ivybridge, Haswell, Broadwell, Skylake."
  echo "Please identify your microarchitecture and hard-code it into the opcodeTesterLauncher.sh file."
  exit 0
  #$microarch="name a supported microarch here"
fi

echo "Detected CPU microarchitecture: $microarch."
echo "If this microarchitecture is wrong, you can hard-code the correct microarchitecture in opcodeTesterLauncher.sh."
echo "Please confirm this microarchitecture is accurate"
read -p "Press Enter to continue or Ctrl+C to quit."

make
cd src/kernel/
make
sudo insmod opcodeTesterKernel.ko					#load kernel driver
cd ..
cd ..

if [[ "$3" = "1" ]]; then
	#get counter vals, : is bash equiv to nop
	port0=$(sudo perf stat -vv -e uops_dispatched_port.port_0 : |& grep 'config ' | awk '{print $2}')
	port1=$(sudo perf stat -vv -e uops_dispatched_port.port_1 : |& grep 'config ' | awk '{print $2}')
	port2=$(sudo perf stat -vv -e uops_dispatched_port.port_2 : |& grep 'config ' | awk '{print $2}')
	port3=$(sudo perf stat -vv -e uops_dispatched_port.port_3 : |& grep 'config ' | awk '{print $2}')
	port4=$(sudo perf stat -vv -e uops_dispatched_port.port_4 : |& grep 'config ' | awk '{print $2}')
	port5=$(sudo perf stat -vv -e uops_dispatched_port.port_5 : |& grep 'config ' | awk '{print $2}')
	port6=$(sudo perf stat -vv -e uops_dispatched_port.port_6 : |& grep 'config ' | awk '{print $2}')
	port7=$(sudo perf stat -vv -e uops_dispatched_port.port_7 : |& grep 'config ' | awk '{print $2}')
	if [[ "$port0" == "" || "$port1" == "" || "$port2" == "" || "$port3" == "" || "$port4" == "" || "$port5" == "" ]]; then
	  echo "Could not detect enough port performance counters; no port analysis possible."
	  portAnalysis="0"
	elif [[ "$port6" == "" || "$port7" == "" ]]; then
	  portAnalysis="1"
	  echo "Will carry out port analysis on performance counters $port0, $port1, $port2, $port3, $port4, $port5."
	else
	  portAnalysis="2"
	  echo "Will carry out port analysis on performance counters $port0, $port1, $port2, $port3, $port4, $port5, $port6, $port7."
	fpRetSingle=$(sudo perf stat -vv -e fp_arith_inst_retired.single : |& grep 'config ' | awk '{print $2}')
	fpRetDouble=$(sudo perf stat -vv -e fp_arith_inst_retired.double : |& grep 'config ' | awk '{print $2}')
	fpRetPacked=$(sudo perf stat -vv -e fp_arith_inst_retired.packed : |& grep 'config ' | awk '{print $2}')
	fpRetScalar=$(sudo perf stat -vv -e fp_arith_inst_retired.scalar : |& grep 'config ' | awk '{print $2}')
	fi
else
	portAnalysis="0"
fi

if [[ "$5" == "0" ]]; then
  testValid="0";
else
  testValid="1";
fi

if [[ "$6" == "0" ]]; then
  testInvalid="0";
else
  testInvalid="1";
fi

if [[ "$7" == "0" ]]; then
  testUndocumented="0";
else
  testUndocumented="1";
fi

if [[ "$8" == "0" ]]; then
  testValidOtherModes="0";
else
  testValidOtherModes="1";
fi

sudo taskset -c 0 ./opcodeTester $1 $2 $portAnalysis $4 $microarch 0 $testValid $testInvalid $testUndocumented $testValidOtherModes $port0 $port1 $port2 $port3 $port4 $port5 $port6 $port7

#enable below to brute-force restart after crashes
#exit_status=$?
#count=0
#while [ $exit_status -ne 0 ]
#do
#((count++))
#echo "Crashed - restarting"
#sleep 2
#sudo taskset -c 0 ./opcodeTester $1 $2 $portAnalysis $4 $microarch 1 $testValid $testInvalid $testUndocumented $testValidOtherModes $port0 $port1 $port2 $port3 $port4 $port5 $port6 $port7			#run execMachineCode locked to one CPU core
#done

sudo rmmod opcodeTesterKernel

echo "Opcode Tester successfully exited."
