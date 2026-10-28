import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import sys
import os

# Check if a benchmark name was provided
if len(sys.argv) < 2:
    print("Usage: script.py <benchmark_name>")
    sys.exit(1)

benchmark_name = sys.argv[1]

# Construct file names
input_file = f'{benchmark_name}.autopsy.csv'
output_file = f'figures/{benchmark_name}.autopsy.png'

# Load data from CSV
df = pd.read_csv(input_file)

# Convert 'time' from seconds to microseconds and round/floor to nearest whole number
df['time_microseconds'] = (df['start time'] * 1e6).round().astype(int)

# Increase the figsize for a larger image, adjust dimensions as needed
plt.figure(figsize=(20, 12))  # Adjusted for larger size, originally (10, 6)

# Iterate over each unique worker
for worker in df['worker'].unique():
    # Filter data for the current worker
    worker_data = df[df['worker'] == worker]
    
    # Aggregate failures by 'time_microseconds' for this worker
    aggregated_data = worker_data.groupby('time_microseconds')['failures'].sum().reset_index()
    
    # Plot the data for this worker with thinner lines
    plt.plot(aggregated_data['time_microseconds'].to_numpy(), aggregated_data['failures'].to_numpy(), marker='o', linestyle='-', linewidth=0.5, label=f'Worker {worker}')  # Adjusted linewidth for thinner lines

plt.title(f'Failures Over Time (Microseconds) for {benchmark_name}')
plt.xlabel('Time (Microseconds)')
plt.ylabel('Number of Failures')
plt.legend()
plt.grid(True)

# Ensure the figures directory exists
if not os.path.exists('figures'):
    os.makedirs('figures')

# Save the figure
plt.savefig(output_file, dpi=300)  # Adjusted dpi for higher resolution

# Optionally, show the plot
# plt.show()

print(f'Figure saved as {output_file}')