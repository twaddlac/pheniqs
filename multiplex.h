/*
    Pheniqs : PHilology ENcoder wIth Quality Statistics
    Copyright (C) 2018  Lior Galanti
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

#ifndef PHENIQS_DEMULTIPLEX_H
#define PHENIQS_DEMULTIPLEX_H

#include "include.h"
#include "pipeline.h"
#include "accumulate.h"
#include "fastq.h"
#include "hts.h"
#include "decoder.h"
#include "metric.h"

class MultiplexJob;
class MultiplexPivot;

class MultiplexJob : public Job {
    friend class MultiplexPivot;
    MultiplexJob(MultiplexJob const &) = delete;
    void operator=(MultiplexJob const &) = delete;

    public:
        MultiplexJob(Document& operation);
        ~MultiplexJob() override;
        inline bool display_distance() const {
            return decode_value_by_key< bool >("display distance", ontology);
        };
        void load() override;
        void start();
        void stop();
        void execute() override;
        void describe(ostream& o) const override;
        bool pull(Read& read);
        void print_compiled(ostream& o) const override;

    protected:
        const Pointer decoder_repository_query;

        void manipulate() override;
        void validate() override;

    private:
        bool end_of_input;
        htsThreadPool thread_pool;
        list< MultiplexPivot > pivot_array;
        list< Feed* > input_feed_by_index;
        list< Feed* > output_feed_by_index;
        vector< Feed* > input_feed_by_segment;
        unordered_map< URL, Feed* > output_feed_by_url;
        void compile_PG();
        void compile_input();
        void detect_input();
        void compile_decoder(Value& value, int32_t& index, const Value& default_decoder, const Value& default_barcode);
        void compile_decoder_group(const Value::Ch* key);
        void compile_output();
        void compile_output_transformation();
        void compile_transformation(Value& value);
        void compile_codec(Value& value, const Value& default_decoder, const Value& default_barcode);
        void compile_decoder_transformation(Value& value);
        bool infer_PU(const Value::Ch* key, string& buffer, Value& container, const bool& undetermined=false);
        bool infer_ID(const Value::Ch* key, string& buffer, Value& container, const bool& undetermined=false);
        void pad_url_array_by_key(const Value::Ch* key, Value& container, const int32_t& cardinality);
        void cross_validate_io();
        void validate_decoder_group(const Value::Ch* key);
        void validate_decoder(Value& value);

        void validate_url_accessibility();
        void load_thread_pool();
        void load_input();
        void load_output();
        void load_pivot();
        void populate_channel(Channel& channel);
        void finalize();

        void print_global_instruction(ostream& o) const;
        void print_codec_group_instruction(const Value::Ch* key, const string& head, ostream& o) const;
        void print_codec_instruction(const Value& value, const bool& plural, ostream& o) const;
        void print_channel_instruction(const string& key, const Value& value, ostream& o) const;
        void print_feed_instruction(const Value::Ch* key, ostream& o) const;
        void print_input_instruction(ostream& o) const;
        void print_transform_instruction(ostream& o) const;
        void print_multiplex_instruction(ostream& o) const;
        void print_molecular_instruction(ostream& o) const;
        void print_cellular_instruction(ostream& o) const;
};

class MultiplexPivot {
    MultiplexPivot(MultiplexPivot const &) = delete;
    void operator=(MultiplexPivot const &) = delete;

    public:
        const int32_t index;
        const Platform platform;
        const int32_t leading_segment_index;
        const int32_t input_segment_cardinality;
        const int32_t output_segment_cardinality;
        Read input;
        Read output;
        RoutingDecoder< Channel >* multiplex;
        vector< Decoder* > molecular;
        vector< Decoder* > cellular;
        InputAccumulator input_accumulator;
        OutputAccumulator output_accumulator;
        MultiplexPivot(MultiplexJob& job, const int32_t& index);
        void start() {
            pivot_thread = thread(&MultiplexPivot::run, this);
        };
        void join() {
            pivot_thread.join();
        };

    protected:
        inline void validate() {
            input.validate();
        };
        inline void transform() {
            template_rule.apply(input, output);
            multiplex->decode(input, output);
            for(auto& decoder : molecular) {
                decoder->decode(input, output);
            }
            for(auto& decoder : cellular) {
                decoder->decode(input, output);
            }
            output.flush();
        };
        inline void push() {
            multiplex->decoded->push(output);
        };
        inline void increment() {
            input_accumulator.increment(input);
            output_accumulator.increment(multiplex->decoded->index, output);
        };
        inline void clear() {
            input.clear();
            output.clear();
        };
        void run() {
            while(job.pull(input)) {
                validate();
                transform();
                push();
                increment();
                clear();
            }
        };

    private:
        MultiplexJob& job;
        thread pivot_thread;
        const bool disable_quality_control;
        const TemplateRule template_rule;
        void load_multiplex_decoding();
        void load_molecular_decoding();
        void load_molecular_decoder(const Value& value);
        void load_cellular_decoding();
        void load_cellular_decoder(const Value& value);

};

#endif /* PHENIQS_DEMULTIPLEX_H */
