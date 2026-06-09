import glob

nt_files = glob.glob("time_NT.*")
t_files  = glob.glob("time_T.*")

if not nt_files or not t_files:
    print("Error: Matching files not found")
    exit(1)

nt_file = nt_files[0]
t_file  = t_files[0]
out_file = "IF_mismatch_timing.txt"

print(f"Using NT file: {nt_file}")
print(f"Using T file: {t_file}")

# Load NT file
nt_map = {}

with open(nt_file) as f:
    for line in f:
        try:
            id_, col2, col3 = map(int, line.split())
            nt_map[id_] = col3
        except ValueError:
            continue

# Compare and output
with open(t_file) as f, open(out_file, "w") as out:
    for line in f:
        try:
            id_, col2_t, col3_t = map(int, line.split())
        except ValueError:
            continue

        if id_ in nt_map:
            if nt_map[id_] != col3_t:
                msg = f"Original_ID {id_}  Transient_version dinstID={col2_t}  IF: Starts at @ClockCycle  NT_version={nt_map[id_]}cc  T_version={col3_t}cc"
                print(msg)
                out.write(msg + "\n")
        else:
            msg = f"Original_ID {id_}, dinstID_T={col2_t} not found in NT"
            print(msg)
            out.write(msg + "\n")

print(f"\nIF mismatch timing results saved to {out_file}")
