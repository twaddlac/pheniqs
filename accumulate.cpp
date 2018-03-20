/*
    Pheniqs : PHilology ENcoder wIth Quality Statistics
    Copyright (C) 2017  Lior Galanti
    NYU Center for Genetics and System Biology

    Author: Lior Galanti <lior.galanti@nyu.edu>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "accumulate.h"

/*  NucleicAcidAccumulator */

NucleicAcidAccumulator::NucleicAcidAccumulator() :
    count(0),
    min(0),
    max(0),
    sum(0),
    mean(0),
    Q1(0),
    Q3(0),
    IQR(0),
    LW(0),
    RW(0),
    median(0) {
    for(size_t i = 0; i < EFFECTIVE_PHRED_RANGE; i++) {
        distribution[i] = 0;
    }
};
inline size_t NucleicAcidAccumulator::quantile(const double portion) {
    size_t position = portion * count;
    size_t cell = 0;
    while (position > 0) {
        if (distribution[cell] >= position) {
            break;
        }
        position -= distribution[cell];
        cell++;
        while (distribution[cell] == 0) {
            cell++;
        }
    }
    return cell;
};
void NucleicAcidAccumulator::finalize() {
    for(size_t i = 0; i < EFFECTIVE_PHRED_RANGE; i++) {
        count += distribution[i];
    }
    if (count > 0) {
        for(size_t i = 0; i < EFFECTIVE_PHRED_RANGE; i++) {
            const uint64_t value = distribution[i];
            sum += (value * i);
            if(value != 0) {
                max = i;
                if(min == 0) {
                    min = i;
                }
            }
        }
        mean = double(sum) / double(count);
        median = quantile(0.5);
        Q1 = quantile(0.25);
        Q3 = quantile(0.75);
        IQR = Q3 - Q1;

        double W = Q1 - IQR * 1.5;
        LW = (W < min) ? min : W;

        W = Q3 + IQR * 1.5;
        RW = (W > max) ? max : W;
    }
};
NucleicAcidAccumulator& NucleicAcidAccumulator::operator+=(const NucleicAcidAccumulator& rhs) {
    for(size_t i = 0; i < EFFECTIVE_PHRED_RANGE; i++) {
        distribution[i] += rhs.distribution[i];
    }
    return *this;
};

/*  CycleAccumulator */

CycleAccumulator::CycleAccumulator() :
    iupac_nucleic_acid(IUPAC_CODE_SIZE) {
};
void CycleAccumulator::finalize() {
    // accumulate all nucleotide variations in the NO_NUCLEOTIDE accumulative distribution
    for(uint8_t i = 1; i < IUPAC_CODE_SIZE; i++) {
        for(uint8_t p = 0; p < EFFECTIVE_PHRED_RANGE; p++) {
            iupac_nucleic_acid[NO_NUCLEOTIDE].distribution[p] += iupac_nucleic_acid[i].distribution[p];
        }
    }
    for(auto& distribution : iupac_nucleic_acid) {
        distribution.finalize();
    }
};
CycleAccumulator& CycleAccumulator::operator+=(const CycleAccumulator& rhs) {
    for(size_t i = 0; i < iupac_nucleic_acid.size(); i++) {
        iupac_nucleic_acid[i] += rhs.iupac_nucleic_acid[i];
    }
    return *this;
};

/*  SegmentAccumulator */

SegmentAccumulator::SegmentAccumulator() :
    count(0),
    min(0),
    max(0),
    sum(0),
    mean(0) {
    for(size_t i = 0; i < EFFECTIVE_PHRED_RANGE; i++) {
        distribution[i] = 0;
    }
};
void SegmentAccumulator::finalize() {
    if(count > 0) {
        mean = sum / double(count);
    }
};
SegmentAccumulator& SegmentAccumulator::operator+=(const SegmentAccumulator& rhs) {
    count += rhs.count;
    sum += rhs.sum;
    min = MIN(min, rhs.min);
    max = MAX(max, rhs.max);
    for(size_t i = 0; i < EFFECTIVE_PHRED_RANGE; i++) {
        distribution[i] += rhs.distribution[i];
    }
    return *this;
};

/*  FeedAccumulator */

FeedAccumulator::FeedAccumulator(const FeedSpecification& specification) :
    // specification(specification),
    url(specification.url),
    length(0),
    shortest(numeric_limits<uint64_t>::max()) {
    for(size_t i = 0; i < IUPAC_CODE_SIZE; i++) {
        iupac_nucleic_acid_count[i] = 0;
    }
};
FeedAccumulator::~FeedAccumulator() {
    for(auto cycle : cycles) {
        delete cycle;
    }
};
void FeedAccumulator::encode(Document& document, Value& value) const {
    Document::AllocatorType& allocator = document.GetAllocator();

    Value v;

    encode_key_value("url", url, value, document);

    v.SetUint64(shortest);
    value.AddMember("min sequence length", v, allocator);

    v.SetUint64(length);
    value.AddMember("max sequence length", v, allocator);

    Value cycle_quality_report;
    cycle_quality_report.SetObject();

    Value cycle_nucleotide_quality_reports;
    cycle_nucleotide_quality_reports.SetArray();

    for (uint8_t n = 0; n < IUPAC_CODE_SIZE; n++) {
        if(iupac_nucleic_acid_count[n] > 0) {
            Value cycle_quality_distribution;
            cycle_quality_distribution.SetObject();

            Value cycle_count;
            cycle_count.SetArray();

            Value cycle_quality_first_quartile;
            cycle_quality_first_quartile.SetArray();

            Value cycle_quality_third_quartile;
            cycle_quality_third_quartile.SetArray();

            Value cycle_quality_interquartile_range;
            cycle_quality_interquartile_range.SetArray();

            Value cycle_quality_left_whisker;
            cycle_quality_left_whisker.SetArray();

            Value cycle_quality_right_whisker;
            cycle_quality_right_whisker.SetArray();

            Value cycle_quality_min;
            cycle_quality_min.SetArray();

            Value cycle_quality_max;
            cycle_quality_max.SetArray();

            Value cycle_quality_mean;
            cycle_quality_mean.SetArray();

            Value cycle_quality_median;
            cycle_quality_median.SetArray();

            for (size_t c = 0; c < cycles.size(); c++) {
                v.SetUint64(cycles[c]->iupac_nucleic_acid[n].count);
                cycle_count.PushBack(v, allocator);

                v.SetUint64(cycles[c]->iupac_nucleic_acid[n].Q1);
                cycle_quality_first_quartile.PushBack(v, allocator);

                v.SetUint64(cycles[c]->iupac_nucleic_acid[n].Q3);
                cycle_quality_third_quartile.PushBack(v, allocator);

                v.SetUint64(cycles[c]->iupac_nucleic_acid[n].IQR);
                cycle_quality_interquartile_range.PushBack(v, allocator);

                v.SetUint64(cycles[c]->iupac_nucleic_acid[n].LW);
                cycle_quality_left_whisker.PushBack(v, allocator);

                v.SetUint64(cycles[c]->iupac_nucleic_acid[n].RW);
                cycle_quality_right_whisker.PushBack(v, allocator);

                v.SetUint64(cycles[c]->iupac_nucleic_acid[n].min);
                cycle_quality_min.PushBack(v, allocator);

                v.SetUint64(cycles[c]->iupac_nucleic_acid[n].max);
                cycle_quality_max.PushBack(v, allocator);

                v.SetDouble(cycles[c]->iupac_nucleic_acid[n].mean);
                cycle_quality_mean.PushBack(v, allocator);

                v.SetUint64(cycles[c]->iupac_nucleic_acid[n].median);
                cycle_quality_median.PushBack(v, allocator);
            }

            cycle_quality_distribution.AddMember("cycle count", cycle_count, allocator);
            cycle_quality_distribution.AddMember("cycle quality first quartile", cycle_quality_first_quartile, allocator);
            cycle_quality_distribution.AddMember("cycle quality third quartile", cycle_quality_third_quartile, allocator);
            cycle_quality_distribution.AddMember("cycle quality interquartile range", cycle_quality_interquartile_range, allocator);
            cycle_quality_distribution.AddMember("cycle quality left whisker", cycle_quality_left_whisker, allocator);
            cycle_quality_distribution.AddMember("cycle quality right whisker", cycle_quality_right_whisker, allocator);
            cycle_quality_distribution.AddMember("cycle quality min", cycle_quality_min, allocator);
            cycle_quality_distribution.AddMember("cycle quality max", cycle_quality_max, allocator);
            cycle_quality_distribution.AddMember("cycle quality mean", cycle_quality_mean, allocator);
            cycle_quality_distribution.AddMember("cycle quality median", cycle_quality_median, allocator);

            if (n > 0) {
                Value cycle_nucleotide_quality_report;
                cycle_nucleotide_quality_report.SetObject();

                Value nucleotide_count;
                nucleotide_count.SetUint64(iupac_nucleic_acid_count[n]);
                cycle_nucleotide_quality_report.AddMember("nucleotide count", nucleotide_count, allocator);

                Value nucleotide;
                nucleotide.SetString(string(1, BamToAmbiguousAscii[n]).c_str(), allocator);
                cycle_nucleotide_quality_report.AddMember("nucleotide", nucleotide, allocator);

                cycle_nucleotide_quality_report.AddMember("cycle quality distribution", cycle_quality_distribution, allocator);
                cycle_nucleotide_quality_reports.PushBack(cycle_nucleotide_quality_report, allocator);

            } else {
                cycle_quality_report.AddMember("cycle quality distribution", cycle_quality_distribution, allocator);
            }
        }
    }
    value.AddMember("cycle nucleotide quality reports", cycle_nucleotide_quality_reports, allocator);
    value.AddMember("cycle nucleotide quality report", cycle_quality_report, allocator);

    Value average_phred_report;
    average_phred_report.SetObject();

    v.SetDouble(average_phred.min);
    average_phred_report.AddMember("average phred score min", v, allocator);

    v.SetDouble(average_phred.max);
    average_phred_report.AddMember("average phred score max", v, allocator);

    v.SetDouble(average_phred.mean);
    average_phred_report.AddMember("average phred score mean", v, allocator);

    Value SegmentAccumulator;
    SegmentAccumulator.SetArray();

    for (size_t i = 0; i < EFFECTIVE_PHRED_RANGE; i++) {
        v.SetUint64(average_phred.distribution[i]);
        SegmentAccumulator.PushBack(v, allocator);
    }
    average_phred_report.AddMember("average phred score distribution", SegmentAccumulator, allocator);
    value.AddMember("average phred score report", average_phred_report, allocator);
};
void FeedAccumulator::finalize() {
    if(shortest == numeric_limits<uint64_t>::max()) {
        shortest = 0;
    }
    for(auto cycle : cycles) {
        cycle->finalize();
    }
    average_phred.finalize();
};
FeedAccumulator& FeedAccumulator::operator+=(const FeedAccumulator& rhs) {
    if(rhs.length > length) {
        for(size_t i = length; i < rhs.length; i++) {
            cycles.push_back(new CycleAccumulator());
        }
        length = rhs.length;
    }

    shortest = MIN(shortest, rhs.shortest);

    for(size_t code = 0; code < IUPAC_CODE_SIZE; code++) {
        iupac_nucleic_acid_count[code] += rhs.iupac_nucleic_acid_count[code];
    }

    for(size_t i = 0; i < rhs.length; i++) {
        *cycles[i] += *rhs.cycles[i];
    }
    average_phred += rhs.average_phred;
    return *this;
};

/*  PivotAccumulator */

PivotAccumulator::PivotAccumulator(const InputSpecification& specification) :
    // specification(specification),
    disable_quality_control(specification.disable_quality_control),
    count(0),
    pf_count(0),
    pf_fraction(0) {

    feed_accumulators.reserve(specification.feed_specifications.size());
    for(auto feed_specification : specification.feed_specifications) {
        feed_accumulators.emplace_back(new FeedAccumulator(*feed_specification));
    }
};
PivotAccumulator::~PivotAccumulator() {
    for(auto feed_accumulator : feed_accumulators) {
        delete feed_accumulator;
    }
};
void PivotAccumulator::finalize() {
    if (count > 0) {
        pf_fraction = double(pf_count) / double(count);
    }
    for(auto& accumulator : feed_accumulators) {
        accumulator->finalize();
    }
};
void PivotAccumulator::encode(Document& document, Value& value) const {
    Document::AllocatorType& allocator = document.GetAllocator();

    Value v;
    v.SetUint64(count);
    value.AddMember("count", v, allocator);

    v.SetUint64(pf_count);
    value.AddMember("pf count", v, allocator);

    v.SetDouble(pf_fraction);
    value.AddMember("pf fraction", v, allocator);

    Value feed_reports;
    feed_reports.SetArray();
    for (auto& accumulator : feed_accumulators) {
        Value feed_report;
        feed_report.SetObject();
        if (!disable_quality_control) {
            accumulator->encode(document, feed_report);
        }
        feed_reports.PushBack(feed_report, allocator);
    }
    value.AddMember("segment quality reports", feed_reports, allocator);
};
PivotAccumulator& PivotAccumulator::operator+=(const PivotAccumulator& rhs) {
    count += rhs.count;
    pf_count += rhs.pf_count;
    for(size_t i = 0; i < feed_accumulators.size(); i++) {
        *(feed_accumulators[i]) += *(rhs.feed_accumulators[i]);
    }
    return *this;
};

/*  ChannelAccumulator */

ChannelAccumulator::ChannelAccumulator(const ChannelSpecification& specification):
    // specification(specification),
    index(specification.index),
    decoder(specification.decoder),
    disable_quality_control(specification.disable_quality_control),
    undetermined(specification.undetermined),
    concentration(specification.concentration),
    multiplex_barcode(specification.multiplex_barcode),
    rg(specification.rg),
    count(0),
    multiplex_distance(0),
    multiplex_confidence(0),
    pf_count(0),
    pf_multiplex_distance(0),
    pf_multiplex_confidence(0),
    pf_fraction(0),
    pooled_fraction(0),
    pf_pooled_fraction(0),
    pooled_multiplex_fraction(0),
    pf_pooled_multiplex_fraction(0),
    accumulated_multiplex_distance(0),
    accumulated_multiplex_confidence(0),
    accumulated_pf_multiplex_distance(0),
    accumulated_pf_multiplex_confidence(0) {

    feed_accumulators.reserve(specification.feed_specification.size());
    for(auto feed_specification : specification.feed_specification) {
        feed_accumulators.emplace_back(new FeedAccumulator(*feed_specification));
    }
};
ChannelAccumulator::~ChannelAccumulator() {
    for(auto feed_accumulator : feed_accumulators) {
        delete feed_accumulator;
    }
};
void ChannelAccumulator::finalize(const PipelineAccumulator& pipeline_accumulator) {
    if (count > 0) {
        multiplex_distance = accumulated_multiplex_distance / double(count);
        multiplex_confidence = accumulated_multiplex_confidence / double(count);
        pf_fraction = double(pf_count) / double(count);
    }
    if (pf_count > 0) {
        pf_multiplex_distance = accumulated_pf_multiplex_distance / double(pf_count);
        pf_multiplex_confidence = accumulated_pf_multiplex_confidence / double(pf_count);
    }
    if (pipeline_accumulator.count > 0) {
        pooled_fraction = double(count) / double(pipeline_accumulator.count);
    }
    if (pipeline_accumulator.pf_count > 0) {
        pf_pooled_fraction = double(pf_count) / double(pipeline_accumulator.pf_count);
    }
    if (pipeline_accumulator.multiplex_count > 0) {
        pooled_multiplex_fraction = double(count) / double(pipeline_accumulator.multiplex_count);
    }
    if (pipeline_accumulator.pf_multiplex_count > 0) {
        pf_pooled_multiplex_fraction = double(pf_count) / double(pipeline_accumulator.pf_multiplex_count);
    }
    for(auto accumulator : feed_accumulators) {
        accumulator->finalize();
    }
};
void ChannelAccumulator::encode(Document& document, Value& value) const {
    Document::AllocatorType& allocator = document.GetAllocator();
    Value v;

    v.SetUint64(index);
    value.AddMember("index", v, allocator);

    if(!undetermined) {
        v.SetDouble(concentration);
        value.AddMember("concentration", v, allocator);

        multiplex_barcode.encode_report(document, value, "multiplex barcode");
    } else {
        v.SetBool(undetermined);
        value.AddMember("undetermined", v, allocator);
    }

    rg.encode(document, value, "RG");

    v.SetUint64(count);
    value.AddMember("count", v, allocator);

    if(!undetermined) {
        v.SetDouble(multiplex_distance);
        value.AddMember("multiplex distance", v, allocator);

        if(decoder == Decoder::PAMLD) {
            v.SetDouble(multiplex_confidence);
            value.AddMember("multiplex confidence", v, allocator);
        }
    }

    v.SetUint64(pf_count);
    value.AddMember("pf count", v, allocator);

    if(!undetermined) {
        v.SetDouble(pf_multiplex_distance);
        value.AddMember("pf multiplex distance", v, allocator);

        if(decoder == Decoder::PAMLD) {
            v.SetDouble(pf_multiplex_confidence);
            value.AddMember("pf multiplex confidence", v, allocator);
        }
    }

    v.SetDouble(pf_fraction);
    value.AddMember("pf fraction", v, allocator);

    v.SetDouble(pooled_fraction);
    value.AddMember("pooled fraction", v, allocator);

    v.SetDouble(pf_pooled_fraction);
    value.AddMember("pf pooled fraction", v, allocator);

    if(!undetermined) {
        v.SetDouble(pooled_multiplex_fraction);
        value.AddMember("pooled multiplex fraction", v, allocator);

        v.SetDouble(pf_pooled_multiplex_fraction);
        value.AddMember("pf pooled multiplex fraction", v, allocator);
    }

    Value feed_reports;
    feed_reports.SetArray();
    for (auto accumulator : feed_accumulators) {
        Value feed_report;
        feed_report.SetObject();
        if (!disable_quality_control) {
            accumulator->encode(document, feed_report);
        }
        feed_reports.PushBack(feed_report, allocator);
    }
    value.AddMember("segment quality reports", feed_reports, allocator);
};
ChannelAccumulator& ChannelAccumulator::operator+=(const ChannelAccumulator& rhs) {
    count += rhs.count;
    pf_count += rhs.pf_count;
    accumulated_multiplex_distance += rhs.accumulated_multiplex_distance;
    accumulated_multiplex_confidence += rhs.accumulated_multiplex_confidence;
    accumulated_pf_multiplex_distance += rhs.accumulated_pf_multiplex_distance;
    accumulated_pf_multiplex_confidence += rhs.accumulated_pf_multiplex_confidence;

    for(size_t i = 0; i < feed_accumulators.size(); i++) {
        *(feed_accumulators[i]) += *(rhs.feed_accumulators[i]);
    }
    return *this;
};

/*  PipelineAccumulator */

PipelineAccumulator::PipelineAccumulator():
    count(0),
    multiplex_count(0),
    multiplex_fraction(0),
    multiplex_distance(0),
    multiplex_confidence(0),
    pf_count(0),
    pf_fraction(0),
    pf_multiplex_count(0),
    pf_multiplex_fraction(0),
    pf_multiplex_distance(0),
    pf_multiplex_confidence(0),
    multiplex_pf_fraction(0),
    accumulated_multiplex_distance(0),
    accumulated_multiplex_confidence(0),
    accumulated_pf_multiplex_distance(0),
    accumulated_pf_multiplex_confidence(0) {
};
void PipelineAccumulator::collect(const ChannelAccumulator& channel_accumulator) {
    count += channel_accumulator.count;
    if (!channel_accumulator.undetermined) {
        multiplex_count += channel_accumulator.count;
        accumulated_multiplex_distance += channel_accumulator.accumulated_multiplex_distance;
        accumulated_multiplex_confidence += channel_accumulator.accumulated_multiplex_confidence;
    }

    pf_count += channel_accumulator.pf_count;
    if (!channel_accumulator.undetermined) {
        pf_multiplex_count += channel_accumulator.pf_count;
        accumulated_pf_multiplex_distance += channel_accumulator.accumulated_pf_multiplex_distance;
        accumulated_pf_multiplex_confidence += channel_accumulator.accumulated_pf_multiplex_confidence;
    }
};
void PipelineAccumulator::finalize() {
    if (count > 0) {
        multiplex_fraction = double(multiplex_count) / double(count);
        multiplex_distance = accumulated_multiplex_distance / double(count);
        multiplex_confidence = accumulated_multiplex_confidence / double(count);
        pf_fraction = double(pf_count) / double(count);
    }
    if (pf_count > 0) {
        pf_multiplex_fraction = double(pf_multiplex_count) / double(pf_count);
        pf_multiplex_distance = accumulated_pf_multiplex_distance / double(pf_count);
        pf_multiplex_confidence = accumulated_pf_multiplex_confidence / double(pf_count);
    }
    if (multiplex_count > 0) {
        multiplex_pf_fraction = double(pf_multiplex_count) / double(multiplex_count);
    }
};
void PipelineAccumulator::encode(Document& document, Value& value) const {
    Document::AllocatorType& allocator = document.GetAllocator();
    Value v;

    v.SetUint64(count);
    value.AddMember("count", v, allocator);

    v.SetUint64(multiplex_count);
    value.AddMember("multiplex count", v, allocator);

    v.SetDouble(multiplex_fraction);
    value.AddMember("multiplex fraction", v, allocator);

    v.SetDouble(multiplex_distance);
    value.AddMember("multiplex distance", v, allocator);

    v.SetDouble(multiplex_confidence);
    value.AddMember("multiplex confidence", v, allocator);

    v.SetUint64(pf_count);
    value.AddMember("pf count", v, allocator);

    v.SetDouble(pf_fraction);
    value.AddMember("pf fraction", v, allocator);

    v.SetUint64(pf_multiplex_count);
    value.AddMember("pf multiplex count", v, allocator);

    v.SetDouble(pf_multiplex_fraction);
    value.AddMember("pf multiplex fraction", v, allocator);

    v.SetDouble(pf_multiplex_distance);
    value.AddMember("pf multiplex distance", v, allocator);

    v.SetDouble(pf_multiplex_confidence);
    value.AddMember("pf multiplex confidence", v, allocator);

    v.SetDouble(multiplex_pf_fraction);
    value.AddMember("multiplex pf fraction", v, allocator);
};