import pandas as pd
import matplotlib.pyplot as plt
import sys
import numpy as np
from tqdm import tqdm

def main(csv_file):
    # Read the CSV file
    df = pd.read_csv(csv_file, header=None, names=['worker', 'start_time', 'end_time', 'status'])

    # Determine the unique workers
    unique_workers = df['worker'].unique()

    # Calculate the total time span
    min_time = df['start_time'].min()
    max_time = df['end_time'].max()
    time_segments = np.arange(min_time, max_time, 0.002)

    # Initialize a dictionary to hold the status for each worker at each time segment
    worker_status = {worker: np.full(len(time_segments), 'idle') for worker in unique_workers}

    # Map statuses to colors
    status_colors = {'work': 'blue', 'inte': 'red', 'idle': 'orange'}

    # Update the status for each worker at each time segment
    for _, row in tqdm(df.iterrows()):
        worker_status[row['worker']][np.where(np.logical_and(time_segments >= row['start_time'],time_segments < row['end_time']))[0]] = row['status']

    # Create a new figure
    plt.figure(figsize=(15, 10))

    # Plot each worker's activity over time
    for i, worker in enumerate(unique_workers):
        statuses = worker_status[worker]
        for status in status_colors.keys():
            times = time_segments[statuses == status]
            if len(times) > 0:
                plt.plot(times, [i]*len(times), '|', color=status_colors[status], markersize=10, label=status if i == 0 else "")

    # Add labels and customize the plot
    plt.yticks(range(len(unique_workers)), unique_workers)
    plt.xlabel('Time (s)')
    plt.ylabel('Workers')
    plt.title('Worker Activity Over Time')
    plt.grid(True)

    # Create a legend
    handles = [plt.Line2D([0], [0], color=color, lw=4) for color in status_colors.values()]
    labels = [status for status in status_colors.keys()]
    plt.legend(handles, labels, loc='upper right')

    # Save the figure
    plt.savefig('figures/autopsy.svg')

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script.py <csv_file>")
        sys.exit(1)
    csv_file = sys.argv[1]
    main(csv_file)
