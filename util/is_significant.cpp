#include "is_significant.hpp"

#include "util/logger.hpp"

#include <cfenv>
#include <numeric>

// Ks computed using a more precise evidence
// std::vector<double> Filter::Ks {
//    -1.59113562621118, -1.43217744601492, -1.47247296586389, -1.47247296625305, -1.47247297329329,
//    -1.47247296625305, -6.98742733670545, -14.9890951260812, -39.0051950910048, -47.0105621583215,
//    -55.0159292256382, -71.0266633602715, -79.0320304275882, -95.0427645622215, -127.064232831488,
//    -119.058865764171, -135.069599898805, -151.080334033438, -175.096435235388, -183.101802302705
//};

// Ks computed using a more precise evidence, plus adding homo+mut
std::vector<double> Filter::Ks = {
    -1.72979993372046, -1.39408158313904, -1.38672090191787, -1.38633789687841, -1.38639629013379,
    -1.38632724169094, -1.38908940299966, -3.59290110171786, -27.4922596296064, -35.497626781593,
    -43.5029937646487, -59.5137279389215, -67.519094962668,  -83.5298290973013, -115.551297366568,
    -107.545930299251, -123.556664433885, -139.567398568518, -163.583499770468, -171.588866837785,
};

const double log_1_4 = std::log(1. / 4);
const double hetero_prior = 0.001;
const double mut_prior = 0.001; // very generous, it's typically 1e-5 or less
const double homo_prior = 1 - hetero_prior - mut_prior; // not hetero and no somatic mutation
const double log_homo_prior = std::log(hetero_prior);
const double log_6 = std::log(6);


Filter::Filter(double theta)
    : theta(theta), log_theta_3(std::log(theta / 3)), log_one_minus_theta(std::log(1 - theta)) {
    log_factorial.reserve(171);
    log_factorial.push_back(1);
    for (uint32_t i = 1; i < 171; ++i) {
        log_factorial.push_back(log_factorial.back() * i);
    }
    for (uint32_t i = 0; i < 171; ++i) {
        log_factorial[i] = std::log(log_factorial[i]);
    }
}

double Filter::log_fact(uint32_t n) {
    return (n > 170) ? 0.5 * std::log(2 * M_PI * n) + n * std::log(n / M_E) : log_factorial.at(n);
}

double round_nearest_even(double x) {
    std::fesetround(FE_TONEAREST);
    return std::nearbyint(x);
}

bool Filter::is_two_sigmas_away(uint32_t coverage, std::array<uint16_t, 4> &base_count) {
    return base_count[3] > 2 * base_count[2] && base_count[2] > 2
            && base_count[2] > (coverage * theta) + 3 * sqrt(coverage * theta * (1 - theta));
}


bool Filter::is_significant(std::array<uint16_t, 4> &base_count) {
    // total coverage
    uint32_t coverage = base_count[0] + base_count[1] + base_count[2] + base_count[3];

    // if there are no reads or if we have just one base, do not keep
    if (coverage < 2) {
        return false;
    }

    // sort base_count; sorts in ascending order
    std::sort(base_count.begin(), base_count.end());

    if (base_count[2] == 0) { // all bases are the same
        return false;
    }

    if (base_count[2] + base_count[1] + base_count[0] < 5) { // not convincing
        return false;
    }

    // this simple heuristic is used for eliminating germline heterozygous loci
    if (base_count[3] < 1.5 * base_count[2]) {
        return false;
    }

    // choose K threshold for the closest coverage, rounding to nearest even to emulate Python
    uint32_t threshold_idx
            = static_cast<uint32_t>(std::clamp(round_nearest_even((coverage / 10.)) - 1, 0., 19.));

    // log P(base_count|wt) for wt=homo
    double log_prob_homozygous
            = base_count[3] * log_one_minus_theta + (coverage - base_count[3]) * log_theta_3;
    // add prior on the most probable genotype (1/4, because all four homozygous genotypes are
    // equally likely)
    log_prob_homozygous += log_1_4;
    // add prior on null hypothesis (0.998)
    log_prob_homozygous += log_homo_prior;
    // the normalizing coefficient (probability of the evidence P(c1,c2,c3,c4)
    // double log_evidence = log_fact(coverage + 3) - log_6 - log_fact(coverage);
    // 1. All true allelles are c1
    double prob_all_c1 = homo_prior * std::pow(1 - theta, base_count[3])
            * std::pow(theta / 3, coverage - base_count[3]);

    // 2. The locus is heterozygous c1 c2
    double prob_hetero = hetero_prior * std::pow(0.5 - theta / 3, base_count[3] + base_count[2])
            * std::pow(theta / 3, base_count[0] + base_count[1]);
    // 3. The locus is homzygous + somatic mutation
    double prob_homo_som = homo_prior * mut_prior * std::pow(0.75 - 2 * theta / 3, base_count[3])
            * std::pow(0.25, base_count[2]) * std::pow(theta / 3, base_count[0] + base_count[1]);

    // 4. The locus is heterozygous + somatic mutation
    double prob_hetero_som = hetero_prior * mut_prior * std::pow(0.5 - theta, base_count[3])
            * std::pow(0.25, base_count[1] + base_count[2]) * std::pow(theta / 3, base_count[0]);
    // 5. Two somatic mutations
    double prob_two_somatic = hetero_prior * mut_prior * mut_prior * std::pow(1 - theta, coverage);
    double log_evidence
            = log(prob_all_c1 + prob_hetero + prob_homo_som + prob_hetero_som + prob_two_somatic);
    return log_prob_homozygous - log_evidence < Ks.at(threshold_idx);
}

bool Filter::is_significant(const PosData &pos_data, uint16_t *coverage) {
    std::array<uint16_t, 4> base_count = { 0, 0, 0, 0 };
    for (uint32_t i = 0; i < pos_data.size(); ++i) {
        base_count[pos_data.base(i)]++;
    }
    *coverage = base_count[0] + base_count[1] + base_count[2] + base_count[3];
    return is_significant(base_count);
}

std::pair<std::vector<std::vector<PosData>>, double>
Filter::filter(const std::vector<std::vector<PosData>> &pos_data,
               const std::vector<uint16_t> &id_to_group,
               const std::vector<uint32_t> &id_to_pos,
               const std::string &marker,
               uint32_t num_threads) {
    std::vector<std::vector<PosData>> result(pos_data.size());
    // one per thread to avoid lock contention
    std::vector<uint32_t> coverage_chr(pos_data.size());
    // can be atomic, very little lock contention
    std::atomic<uint32_t> total_positions = 0;
    std::ignore = num_threads;
#pragma omp parallel for num_threads(num_threads)
    for (uint32_t chr_idx = 0; chr_idx < pos_data.size(); ++chr_idx) {
        std::vector<PosData> filtered_data;
        for (uint32_t pos_idx = 0; pos_idx < pos_data[chr_idx].size(); ++pos_idx) {
            std::vector<uint32_t> read_ids;
            std::vector<uint16_t> cell_ids_and_bases;
            const PosData &pd = pos_data[chr_idx][pos_idx];
            std::array<uint16_t, 4> base_count = { 0, 0, 0, 0 };
            for (uint32_t cell_idx = 0; cell_idx < pd.size(); ++cell_idx) {
                if (id_to_pos[id_to_group[pd.cell_id(cell_idx)]] == NO_POS) {
                    continue;
                }
                read_ids.push_back(pd.read_ids[cell_idx]);
                cell_ids_and_bases.push_back(pd.cell_ids_bases[cell_idx]);
                base_count[pd.base(cell_idx)]++;
            }

            if (is_significant(base_count)) {
                coverage_chr[chr_idx] += read_ids.size();
                filtered_data.emplace_back(pd.position, std::move(read_ids),
                                           std::move(cell_ids_and_bases));
            }
        }
        total_positions.fetch_add(filtered_data.size());
        result[chr_idx] = std::move(filtered_data);
    }

    uint32_t total_coverage = std::accumulate(coverage_chr.begin(), coverage_chr.end(), 0);
    double avg_coverage
            = total_positions == 0 ? 0 : static_cast<double>(total_coverage) / total_positions;
    logger()->trace("Avg coverage for cluster {}: {}. Total positions: {}", marker, avg_coverage,
                    total_positions);
    return { result, avg_coverage };
}
