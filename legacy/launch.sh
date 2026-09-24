#!/bin/bash



SERVER='192.168.0.41'
CMDLINE="./buddhaBrot/buddha -p 100000000 -S $SERVER -s 4000 -N 262144 --nosave &"
HOSTS=('lulu')



for i in $( seq 0 $(expr ${#HOSTS[*]} - 1) ); do
	for j in $(seq 1 12); do
		ssh ${HOSTS[$i]}$j $CMDLINE &
	done
done

#./buddha-server
