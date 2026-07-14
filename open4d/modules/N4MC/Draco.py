import re
import subprocess
import time
import os
dataset = "mixed"
Ground_truth_path = "/mnt/datadrive/ChromeDownloads/Mesh_dataset/combined_scaled/gt"
input_dir = Ground_truth_path
output_root_dir = os.path.join(r'/mnt/datadrive/ChromeDownloads/Mesh_dataset/encode_Draco', dataset)

obj_files = [f for f in os.listdir(input_dir) if f.endswith('.obj')]

qps = range(7, 8)

for qp in qps:
    times = []
    output_dir = os.path.join(output_root_dir, f'{dataset}_qp_{qp}')
    os.makedirs(output_dir, exist_ok=True)

    output_file = os.path.join(output_dir, f'encoding_times_qp_{qp}.txt')
    with open(output_file, 'w') as f:
        total_encoding_time = 0
        for obj_file in obj_files[0:100]:

            input_path = os.path.join(input_dir, obj_file)
            output_path = os.path.join(output_dir, obj_file.replace('.obj', f'_qp_{qp}.drc'))

            start_time = time.time()

            result = subprocess.run([
                            r'/home/frozzzen/Documents/Github_SINRG/TSMC/draco/build/draco_encoder',
                            '-i', input_path,
                            '-o', output_path,
                            '-qp', str(qp)
                            ], capture_output=True, text=True)
            print(result.stdout)

            # List to store extracted times

            # Regular expression to match the encoding time
            time_pattern = re.compile(r"\((\d+) ms to encode\)")

            # Loop through each line and extract the time
            match = time_pattern.search(result.stdout)
            if match:
                times.append(int(match.group(1)))

            end_time = time.time()

            encoding_time = end_time - start_time
            total_encoding_time += encoding_time
            #print(f"Encoded {obj_file} in {encoding_time:.4f} seconds")
            #f.write(f"Encoded {obj_file} in {encoding_time:.4f} seconds\n")

        if times:
                mean_time = sum(times) / len(times)
                print(f"Mean encoding time: {mean_time:.2f} ms")
        total_files = len(obj_files)


        #f.write(f"\nTotal encoding time for qp {qp}: {total_encoding_time:.4f} seconds\n")
        f.write(f"Average encoding time for qp {qp}: {mean_time:.2f} seconds\n\n")


input_root_dir = output_root_dir
output_root_dir = os.path.join(r'/mnt/datadrive/ChromeDownloads/Mesh_dataset/decode_Draco', dataset)
os.makedirs(output_root_dir, exist_ok=True)



for qp in qps:
    times = []
    output_dir = os.path.join(output_root_dir, f'{dataset}_qp_{qp}')
    os.makedirs(output_dir, exist_ok=True)
    input_dir = os.path.join(input_root_dir, f'{dataset}_qp_{qp}')
    drc_files = [f for f in os.listdir(input_dir) if f.endswith('.drc')]
    output_file = os.path.join(output_dir, f'decoding_times_qp_{qp}.txt')
    with open(output_file, 'w') as f:
        total_decoding_time = 0
        for drc_file in drc_files[0:100]:
            input_path = os.path.join(input_dir, drc_file)
            output_path = os.path.join(output_dir, drc_file.replace(f'_qp_{qp}.drc', f'_qp_{qp}_decoded.obj'))
            start_time = time.time()

            # Use subprocess to execute the draco_decoder command
            result = subprocess.run([
                                    r'/home/frozzzen/Documents/Github_SINRG/TSMC/draco/build/draco_decoder',
                                    '-i', input_path,
                                    '-o', output_path
                                    ], capture_output=True, text=True)
            print(result.stdout)

            end_time = time.time()
            time_pattern = re.compile(r"\((\d+) ms to decode\)")

            # Loop through each line and extract the time
            match = time_pattern.search(result.stdout)
            if match:
                times.append(int(match.group(1)))

            decoding_time = end_time - start_time
            total_decoding_time += decoding_time

        total_files = len(drc_files)
        if times:
                mean_time = sum(times) / len(times)
                print(f"Mean decoding time: {mean_time:.2f} ms")
        total_files = len(obj_files)


        #f.write(f"\nTotal encoding time for qp {qp}: {total_encoding_time:.4f} seconds\n")
        f.write(f"Average decoding time for qp {qp}: {mean_time:.2f} seconds\n\n")


