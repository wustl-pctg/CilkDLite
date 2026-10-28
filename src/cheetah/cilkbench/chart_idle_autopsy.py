import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import sys
import os

def pad_and_average(array, group_size):
    # Calculate the padding needed
    padding_needed = (group_size - len(array) % group_size) % group_size
    
    # Pad the array with the last item
    if padding_needed > 0:
        pad_value = array[-1]
        padded_array = np.pad(array, (0, padding_needed), mode='constant', constant_values=pad_value)
    else:
        padded_array = array

    # Reshape the array to have group_size columns
    reshaped_array = padded_array.reshape(-1, group_size)

    # Calculate the mean across the columns
    averaged_array = reshaped_array.mean(axis=1)

    return averaged_array


def process_data(file_name):
    # Load the data
    df = pd.read_csv(file_name, header=None, names=['Worker', 'Start', 'End', 'Status'])
    
    # Determine the overall time range
    overall_start = df['Start'].min()
    overall_end = df['End'].max()
    sample_frequency = 0.00002
    averaged_samples = 50
    # Create a time series with 0.001s intervals
    time_series = np.arange(overall_start, overall_end, sample_frequency)
    total_working_time = np.zeros_like(time_series)
    total_interrupted_time = np.zeros_like(time_series)
    ratio_interrupted = np.zeros_like(time_series)
    # Calculate total working and interrupted time for each time segment
    for _, row in df.iterrows():
        time_range = (time_series >= row['Start']) & (time_series < row['End'])
        if row['Status'] == 'working':
            total_working_time += time_range
        elif row['Status'] == 'interupted':
            total_interrupted_time += time_range
    # Calculate total idle time
    non_idle_time = (total_working_time + total_interrupted_time)
    total_idle_time = len(df['Worker'].unique()) - non_idle_time
    # Normalize by the number of workers
    number_of_workers = df['Worker'].nunique()
    average_working_time = total_working_time / number_of_workers
    average_interrupted_time = total_interrupted_time / number_of_workers
    average_idle_time = total_idle_time / number_of_workers
    
    # Calculate the ratio of interrupted time to (working time + interrupted time)
    non_idle_time[non_idle_time==0] = 1
    ratio_interrupted = total_interrupted_time / non_idle_time
    # Plot the results
    fig, ax1 = plt.subplots(figsize=(12, 6))

    bucketed_ratio_interrupted = pad_and_average(ratio_interrupted, averaged_samples)
    bucketed_time_series = pad_and_average(time_series, averaged_samples)
    bucketed_average_working_time = pad_and_average(average_working_time, averaged_samples)
    bucketed_average_idle_time = pad_and_average(average_idle_time, averaged_samples)
    bucketed_average_interrupted_time = pad_and_average(average_interrupted_time, averaged_samples)
    
    ax1.stackplot(bucketed_time_series, bucketed_average_working_time, bucketed_average_idle_time, bucketed_average_interrupted_time, labels=['Working Time', 'Idle Time', 'Interrupted Time'])
    ax1.set_xlabel('Time (s)')
    ax1.set_ylabel('Average Time')
    ax1.set_title(f"Average Time per {sample_frequency*averaged_samples}s Time Segment - averaged over {averaged_samples} samples in {file_name}")
    ax1.set_ylim(bottom=0)  # Ensure zero is at the bottom of the graph
    
    # Move the legend off the plot
    ax1.legend(loc='center left', bbox_to_anchor=(1, 0.5))
    ax1.grid(True)
    
    # Add a second y-axis for the ratio
    ax2 = ax1.twinx()
    ax2.plot(bucketed_time_series, bucketed_ratio_interrupted, color='red', label='Interrupted / (Working + Interrupted)', linewidth=0.4)
    ax2.set_ylabel('Ratio of Interrupted Time')
    ax2.legend(loc='center left', bbox_to_anchor=(1, 0.6))
    ax2.set_ylim(bottom=0, top=1)  # Ensure zero is at the bottom of the graph

    # Ensure the 'figures' directory exists
    if not os.path.exists('figures'):
        os.makedirs('figures')
    
    # Save the figure
    plt.savefig(f"figures/{file_name}.png", bbox_inches='tight')

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script.py <csv_file>")
        sys.exit(1)
    
    csv_file = sys.argv[1]
    process_data(csv_file)
