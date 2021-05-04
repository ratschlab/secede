# Runs the variant caller on pileup files (either binary generated by our own pileup binary, or textual generated by samtools mpileup)

module load openblas
source global_vars.sh

work_dir="${base_dir}/${cov}"
input_dir="${work_dir}/pileups"
svc="${code_dir}/build/svc"
flagfile="$HOME/somatic_variant_calling/code/flags_sim"
out_dir=${work_dir}/svc/
mkdir -p "${out_dir}"
command="$svc -i ${input_dir}/ -o ${out_dir} --num_threads 20 --log_level=trace --flagfile ${flagfile} \
         --clustering_type SPECTRAL6 --merge_count 10 --max_coverage 100 | tee ${out_dir}/svc.log"
echo "$command"

bsub  -J "svc" -W 01:00 -n 20 -R "rusage[mem=8000]" -R "span[hosts=1]" -oo "${out_dir}/svc.lsf.log" "${command}"
