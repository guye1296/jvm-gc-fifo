#!/bin/bash

source configuration.sh

sshpass -p $pass scp $local_project_path/src/* $usr_name@nova.cs.tau.ac.il:$remote_project_path
sshpass -p $pass scp /$local_project_path/includes/* $usr_name@nova.cs.tau.ac.il:$remote_project_path
sshpass -p $pass scp $local_project_path/scripts/* $usr_name@nova.cs.tau.ac.il:$remote_project_path
sshpass -p $pass scp $local_project_path/MSQUEUE/* $usr_name@nova.cs.tau.ac.il:$remote_project_path
sshpass -p $pass scp $local_project_path/LFSTACK/* $usr_name@nova.cs.tau.ac.il:$remote_project_path
sshpass -p $pass scp $local_project_path/HLCRQ/* $usr_name@nova.cs.tau.ac.il:$remote_project_path