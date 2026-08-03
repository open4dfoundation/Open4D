import os

def run_binary(filename, tar_f, num_subd):
    bin_path = "ssp_remesh/build/ssp_remesh_bin"
    filename = os.path.join("objs_original", filename+".obj")
    cmd = f"{bin_path} {filename} {tar_f} {num_subd} 0"
    os.system(cmd)

    mesh_name = os.path.splitext(os.path.basename(filename))[0]
    experiments_dir = "experiments"
    mesh_dir = os.path.join(experiments_dir, mesh_name)
    
    os.makedirs(mesh_dir, exist_ok=True)
    os.system(f"mv input_* {mesh_dir}")
    os.system(f"mv output_* {mesh_dir}")

# run_binary("objs_original/dragon.obj", 1000, 1)
# run_binary("objs_original/dragon.obj", 2000, 1)
# run_binary("objs_original/dragon.obj", 4000, 1)
# run_binary("objs_original/dragon.obj", 8000, 1)
# run_binary("objs_original/dragon.obj", 16000, 1)