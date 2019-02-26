#!/usr/bin/expect

set pass [exec cat configuration.sh | grep "pass" | cut -d "=" -f2 | sed -e "s/^\"//" -e "s/\"$//"]
set usr_name [exec cat configuration.sh | grep "usr_name" | cut -d "=" -f2 | sed -e "s/^\"//" -e "s/\"$//"]
set remote_machine [exec cat configuration.sh | grep "remote_machine" | cut -d "=" -f2 | sed -e "s/^\"//" -e "s/\"$//"]
puts $pass
puts $usr_name
puts $remote_machine
spawn bash -c "sshpass -p $pass ssh -A -t $usr_name@nova.cs.tau.ac.il ssh -A -t $usr_name@$remote_machine.cs.tau.ac.il"
expect {
password: {send "$pass\r"; exp_continue}
}
interact
