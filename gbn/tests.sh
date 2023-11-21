#!/bin/bash

# Define parameters
loss_corruption_params=(0.1 0.2 0.3 0.4 0.5)
seeds=($(seq 1 100 10001))
args=(1000 0 0 200 8 30 1 1)

# Iterate through parameters for variable loss.
for param in "${loss_corruption_params[@]}"; do
	# Adjust loss variable.
	args[1]=$param

	# Start printing RTT results for this set of variables.
	python_param=$(echo $param | awk -F . '{print $2}')
	echo -n "rtt_loss_0${python_param}s = ["

	# Iterate through each seed.
	for i in $(seq "${#seeds[@]}"); do
		args[7]="${seeds[$i - 1]}"
		results=$(./pa2_gbn <<< $(printf '%s\n' "${args[@]}") | grep "Average RTT" | awk -F : '{print $2}')
		echo -n "$results, "
	done

	echo ']'

	# Start printing Communication results for this set of variables.
	python_param=$(echo $param | awk -F . '{print $2}')
	echo -n "time_loss_0${python_param}s = ["

	# Iterate through each seed.
	for i in $(seq "${#seeds[@]}"); do
		args[7]="${seeds[$i - 1]}"
		results=$(./pa2_gbn <<< $(printf '%s\n' "${args[@]}") | grep "Average comm" | awk -F : '{print $2}')
		echo -n "$results, "
	done

	echo ']'
done

echo '--------------------------------------------------'

# Iterate through parameters for corruption loss.
for param in "${loss_corruption_params[@]}"; do
	# Adjust corruption variable.
	args[2]=$param

	# Start printing RTT results for this set of variables.
	python_param=$(echo $param | awk -F . '{print $2}')
	echo -n "rtt_corruption_0${python_param}s = ["

	# Iterate through each seed.
	for i in $(seq "${#seeds[@]}"); do
		args[7]="${seeds[$i - 1]}"
		results=$(./pa2_gbn <<< $(printf '%s\n' "${args[@]}") | grep "Average RTT" | awk -F : '{print $2}')
		echo -n "$results, "
	done

	echo ']'

	# Start printing Communication results for this set of variables.
	python_param=$(echo $param | awk -F . '{print $2}')
	echo -n "time_corruption_0${python_param}s = ["

	# Iterate through each seed.
	for i in $(seq "${#seeds[@]}"); do
		args[7]="${seeds[$i - 1]}"
		results=$(./pa2_gbn <<< $(printf '%s\n' "${args[@]}") | grep "Average comm" | awk -F : '{print $2}')
		echo -n "$results, "
	done

	echo ']'
done
