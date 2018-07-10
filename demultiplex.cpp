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

#include "demultiplex.h"

static int32_t inheritence_depth(const string& key, const unordered_map< string, Value* >& node_by_key, Document& document) {
    int32_t depth(0);
    auto record = node_by_key.find(key);
    if(record != node_by_key.end()) {
        Value* value = record->second;
        if(!decode_value_by_key("depth", depth, *value)) {
            string base_key;
            if(decode_value_by_key("base", base_key, *value)) {
                if(base_key != key) {
                    depth = inheritence_depth(base_key, node_by_key, document) + 1;
                    encode_key_value("depth", depth, *value, document);
                } else { throw ConfigurationError("object can not inherit from itself " + key); }
            } else {
                encode_key_value("depth", depth, *value, document);
            }
        }
    } else { throw ConfigurationError("referencing an undefined base decoder " + key); }
    return depth;
};

Demultiplex::~Demultiplex() {
    if(thread_pool.pool != NULL) {
        hts_tpool_destroy(thread_pool.pool);
    }
    for(auto feed : input_feed_by_index) {
        delete feed;
    }
    for(auto feed : output_feed_by_index) {
        delete feed;
    }
    input_feed_by_segment.clear();
    input_feed_by_index.clear();
    output_feed_by_index.clear();
};
void Demultiplex::manipulate() {
    compile_PG_element();
    compile_input_instruction();
    apply_decoder_inheritence();
    compile_decoder_group_instruction("multiplex");
    compile_decoder_group_instruction("molecular");
    compile_decoder_group_instruction("splitseq");
    compile_output_instruction();
};
void Demultiplex::clean() {
    ontology.RemoveMember("decoder");
    Job::clean();
};
void Demultiplex::validate() {
    Job::validate();

    uint8_t input_phred_offset;
    if(decode_value_by_key< uint8_t >("input phred offset", input_phred_offset, ontology)) {
        if(input_phred_offset > MAX_PHRED_VALUE || input_phred_offset < MIN_PHRED_VALUE) {
            throw ConfigurationError("input phred offset out of range " + to_string(input_phred_offset));
        }
    }

    uint8_t output_phred_offset;
    if(decode_value_by_key< uint8_t >("output phred offset", output_phred_offset, ontology)) {
        if(output_phred_offset > MAX_PHRED_VALUE || output_phred_offset < MIN_PHRED_VALUE) {
            throw ConfigurationError("output phred offset out of range " + to_string(output_phred_offset));
        }
    }

    validate_codec_group_sanity("multiplex");
    validate_codec_group_sanity("molecular");
    validate_codec_group_sanity("splitseq");
};
void Demultiplex::describe(ostream& o) const {
    print_global_instruction(o);
    print_input_instruction(o);
    print_template_instruction(o);
    print_multiplex_instruction(o);
    print_molecular_instruction(o);
    print_splitseq_instruction(o);
};
void Demultiplex::execute() {
    if(is_validate_only()) {
        describe(cerr);
    } else if(is_lint_only()) {
        print_ontology(cout);
    } else {
        load();
        start();
        stop();
        finalize();
        print_report(cerr);
    }
};
void Demultiplex::start() {
    for(auto feed : input_feed_by_index) {
        feed->open();
    }
    for(auto feed : output_feed_by_index) {
        feed->open();
    }
    for(auto feed : input_feed_by_index) {
        feed->start();
    }
    for(auto feed : output_feed_by_index) {
        feed->start();
    }
    for(auto& pivot : pivot_array) {
        pivot.start();
    }
    for(auto& pivot : pivot_array) {
        pivot.join();
    }
};
void Demultiplex::stop() {
    /*
        output channel buffers still have residual records
        notify all output feeds that no more input is coming
        and they should explicitly flush
    */
    for(auto feed : output_feed_by_index) {
        feed->stop();
    }
    for(auto feed : input_feed_by_index) {
        feed->join();
    }
    for(auto feed : output_feed_by_index) {
        feed->join();
    }
};
void Demultiplex::finalize() {
    Value value;
    value.CopyFrom(ontology, report.GetAllocator());
    report.AddMember(Value("job", report.GetAllocator()).Move(), value.Move(), report.GetAllocator());

    InputAccumulator input_accumulator(ontology);
    OutputAccumulator output_accumulator(find_value_by_key("multiplex", ontology));
    for(auto& pivot : pivot_array) {
        input_accumulator += pivot.input_accumulator;
        output_accumulator += pivot.output_accumulator;
    }
    input_accumulator.finalize();
    output_accumulator.finalize();
    encode_key_value("demultiplex output report", output_accumulator, report, report);
    encode_key_value("demultiplex input report", input_accumulator, report, report);

    clean_json_value(report, report);
    sort_json_value(report, report);
};
bool Demultiplex::pull(Read& read) {
    vector< unique_lock< mutex > > feed_locks;
    feed_locks.reserve(input_feed_by_index.size());

    // acquire a pull lock for all feeds in a fixed order
    for(const auto feed : input_feed_by_index) {
        feed_locks.push_back(feed->acquire_pull_lock());
    }

    // pull into pivot input segments from input feeds
    for(size_t i = 0; i < read.segment_cardinality(); ++i) {
        if(!input_feed_by_segment[i]->pull(read[i])) {
            end_of_input = true;
        }
    }

    // release the locks on the feeds in reverse order
    for(auto feed_lock = feed_locks.rbegin(); feed_lock != feed_locks.rend(); ++feed_lock) {
        feed_lock->unlock();
    }
    return !end_of_input;
};

void Demultiplex::compile_PG_element() {
    Value PG(kObjectType);

    string buffer;
    if(decode_value_by_key< string >("application name", buffer, ontology)) {
        encode_key_value("ID", buffer, PG, ontology);
    }
    if(decode_value_by_key< string >("application name", buffer, ontology)) {
        encode_key_value("PN", buffer, PG, ontology);
    }
    if(decode_value_by_key< string >("full command", buffer, ontology)) {
        encode_key_value("CL", buffer, PG, ontology);
    }
    if(decode_value_by_key< string >("previous application", buffer, ontology)) {
        encode_key_value("PP", buffer, PG, ontology);
    }
    if(decode_value_by_key< string >("application description", buffer, ontology)) {
        encode_key_value("DS", buffer, PG, ontology);
    }
    if(decode_value_by_key< string >("application version", buffer, ontology)) {
        encode_key_value("VN", buffer, PG, ontology);
    }
    ontology.AddMember(Value("program", ontology.GetAllocator()).Move(), PG.Move(), ontology.GetAllocator());
};
void Demultiplex::compile_input_instruction() {
    expand_url_value_by_key("base input url", ontology, ontology);
    expand_url_array_by_key("input", ontology, ontology, IoDirection::IN);
    URL base(decode_value_by_key< URL >("base input url", ontology));

    relocate_url_array_by_key("input", ontology, ontology, base);
    list< URL > feed_url_array(decode_value_by_key< list< URL > >("input", ontology));

    Platform platform(decode_value_by_key< Platform >("platform", ontology));
    int32_t buffer_capacity(decode_value_by_key< int32_t >("buffer capacity", ontology));
    uint8_t input_phred_offset(decode_value_by_key< uint8_t >("input phred offset", ontology));

    /* encode the input segment cardinality */
    int32_t input_segment_cardinality(static_cast< int32_t>(feed_url_array.size()));
    encode_key_value("input segment cardinality", input_segment_cardinality, ontology, ontology);

    /*  validate leading_segment_index */
    int32_t leading_segment_index(decode_value_by_key< int32_t >("leading segment index", leading_segment_index, ontology));
    if(leading_segment_index >= input_segment_cardinality) {
        throw ConfigurationError("leading segment index " + to_string(leading_segment_index) + " references non existing input segment");
    }

    map< URL, int > feed_resolution;
    for(const auto& url : feed_url_array) {
        ++(feed_resolution[url]);
    }

    int32_t feed_index(0);
    unordered_map< URL, Value > feed_ontology_by_url;
    for(const auto& record : feed_resolution) {
        const URL& url = record.first;
        int resolution(record.second);
        Value proxy(kObjectType);
        encode_key_value("index", feed_index, proxy, ontology);
        encode_key_value("url", record.first, proxy, ontology);
        encode_key_value("direction", IoDirection::IN, proxy, ontology);
        encode_key_value("platform", platform, proxy, ontology);
        encode_key_value("capacity", buffer_capacity * resolution, proxy, ontology);
        encode_key_value("resolution", resolution, proxy, ontology);
        encode_key_value("phred offset", input_phred_offset, proxy, ontology);
        feed_ontology_by_url.emplace(make_pair(url, move(proxy)));
        ++feed_index;
    }

    Value feed_by_segment(kArrayType);
    for(const auto& url : feed_url_array) {
        const Value& proxy(feed_ontology_by_url[url]);
        feed_by_segment.PushBack(Value(proxy, ontology.GetAllocator()).Move(), ontology.GetAllocator());
    }
    ontology.RemoveMember("input feed by segment");
    ontology.AddMember("input feed by segment", feed_by_segment.Move(), ontology.GetAllocator());

    Value feed_array(kArrayType);
    for(auto& record : feed_ontology_by_url) {
        feed_array.PushBack(record.second.Move(), ontology.GetAllocator());
    }
    ontology.RemoveMember("input feed");
    ontology.AddMember("input feed", feed_array.Move(), ontology.GetAllocator());
};
void Demultiplex::apply_decoder_inheritence() {
    Value::MemberIterator reference = ontology.FindMember("decoder");
    if(reference != ontology.MemberEnd()) {
        if(!reference->value.IsNull()) {
            if(reference->value.IsObject()) {

                /*  map each decoder by key */
                unordered_map< string, Value* > codec_by_key;
                for(auto& record : reference->value.GetObject()) {
                    if(!record.value.IsNull()) {
                        record.value.RemoveMember("depth");
                        codec_by_key.emplace(make_pair(string(record.name.GetString(), record.name.GetStringLength()), &record.value));
                    }
                }

                /* compute the inheritence depth of each decoder and keep track of the max depth */
                int32_t max_depth(0);
                for(auto& record : codec_by_key) {
                    try {
                        max_depth = max(max_depth, inheritence_depth(record.first, codec_by_key, ontology));
                    } catch(ConfigurationError& error) {
                        throw CommandLineError("decoder " + record.first + " is " + error.message);
                    }
                }

                /* apply decoder inheritence down the tree */
                int32_t depth(0);
                for(int32_t i(1); i <= max_depth; ++i) {
                    for(auto& record : codec_by_key) {
                        Value* value = record.second;
                        if(decode_value_by_key("depth", depth, *value) && depth == i) {
                            string base;
                            if(decode_value_by_key< string >("base", base, *value)) {
                                merge_json_value(*codec_by_key[base], *value, ontology);
                            }
                        }
                    }
                }
            } else { throw ConfigurationError("decoder element must be a dictionary"); }
        }
    }
};
void Demultiplex::compile_decoder_group_instruction(const Value::Ch* key) {
    Value::MemberIterator reference = ontology.FindMember(key);
    if(reference != ontology.MemberEnd()) {
        if(!reference->value.IsNull()) {
            Value& node(reference->value);

            /* get the base decoder and codec node from the configuration */
            Value default_configuration_decoder(kObjectType);
            Value default_configuration_codec(kObjectType);

            Value::MemberIterator reference = ontology.FindMember("operation");
            if(reference != ontology.MemberEnd()) {
                if(reference->value.IsObject()) {
                    Value& operation(reference->value);
                    reference = operation.FindMember("projection");
                    if(reference != operation.MemberEnd()) {
                        Value& projection(reference->value);
                        if(!projection.IsNull()) {
                            reference = projection.FindMember("decoder");
                            if(reference != projection.MemberEnd() && !reference->value.IsNull()) {
                                default_configuration_decoder.CopyFrom(reference->value, ontology.GetAllocator());
                            }
                            reference = projection.FindMember("codec");
                            if(reference != projection.MemberEnd() && !reference->value.IsNull()) {
                                default_configuration_codec.CopyFrom(reference->value, ontology.GetAllocator());
                            }
                        }
                    }
                }
            }

            /* project decoder attributes from the document root */
            Value default_instruction_decoder(kObjectType);
            project_json_value(default_configuration_decoder, ontology, default_instruction_decoder, ontology);
            default_configuration_decoder.SetNull();

            /* project codec attributes from the ontology root */
            Value default_instruction_codec(kObjectType);
            project_json_value(default_configuration_codec, ontology, default_instruction_codec, ontology);
            default_configuration_codec.SetNull();

            /* create a map of globally available codecs */
            unordered_map< string, Value* > codec_by_key;
            reference = ontology.FindMember("decoder");
            if(reference != ontology.MemberEnd()) {
                if(!reference->value.IsNull()) {
                    if(reference->value.IsObject()) {
                        for(auto& record : reference->value.GetObject()) {
                            if(!record.value.IsNull()) {
                                string key(record.name.GetString(), record.name.GetStringLength());
                                codec_by_key.emplace(make_pair(key, &record.value));
                            }
                        }
                    }
                }
            }

            if(node.IsObject()) {
                string base;
                if(decode_value_by_key< string >("base", base, node)) {
                    auto record = codec_by_key.find(base);
                    if(record != codec_by_key.end()) {
                        merge_json_value(*record->second, node, ontology);
                    } else { throw ConfigurationError(string(key) + " is referencing an undefined base decoder " + base); }
                }
                encode_key_value("index", 0, node, ontology);
                compile_decoder_codec(node, default_instruction_decoder, default_instruction_codec);
                compile_decoder_transformation(node);

            } else if(node.IsArray()) {
                int32_t index(0);
                for(auto& element : node.GetArray()) {
                    if(element.IsObject()) {
                        string base;
                        if(decode_value_by_key< string >("base", base, element)) {
                            auto record = codec_by_key.find(base);
                            if(record != codec_by_key.end()) {
                                merge_json_value(*record->second, element, ontology);
                            } else { throw ConfigurationError("element " + to_string(index) + " of " + string(key) + " is referencing an undefined base decoder " + base); }
                        }
                        encode_key_value("index", index, element, ontology);
                        compile_decoder_codec(element, default_instruction_decoder, default_instruction_codec);
                        compile_decoder_transformation(element);
                        ++index;
                    } else { throw ConfigurationError("decoder element must be a dictionary"); }
                }
            }
            // compile_decoder_group(key);

            default_instruction_decoder.SetNull();
            default_instruction_codec.SetNull();
        }
    }
};
void Demultiplex::compile_output_instruction() {
    /* load output template */
    const int32_t input_segment_cardinality(decode_value_by_key< int32_t >("input segment cardinality", ontology));
    compile_transformation(ontology);

    Rule rule(decode_value_by_key< Rule >("template", ontology));
    for(auto& token : rule.token_array) {
        if(!(token.input_segment_index < input_segment_cardinality)) {
            throw ConfigurationError("invalid input feed reference " + to_string(token.input_segment_index) + " in token " + to_string(token.index));
        }
    }
    const int32_t output_segment_cardinality(rule.output_segment_cardinality);
    encode_key_value("output segment cardinality", output_segment_cardinality, ontology, ontology);

    Value::MemberIterator reference = ontology.FindMember("multiplex");
    if(reference != ontology.MemberEnd()) {
        if(reference->value.IsObject()) {
            Value& value(reference->value);
            expand_url_value_by_key("base output url", value, ontology);
            URL base(decode_value_by_key< URL >("base output url", value));

            Platform platform(decode_value_by_key< Platform >("platform", ontology));
            int32_t buffer_capacity(decode_value_by_key< int32_t >("buffer capacity", ontology));
            uint8_t phred_offset(decode_value_by_key< uint8_t >("output phred offset", ontology));

            unordered_map< URL, unordered_map< int32_t, int > > feed_resolution;
            Value::MemberIterator reference = value.FindMember("undetermined");
            if(reference != value.MemberEnd()) {
                if(!reference->value.IsNull()) {
                    int32_t index(decode_value_by_key< int32_t >("index", reference->value));
                    encode_key_value("TC", output_segment_cardinality, reference->value, ontology);
                    pad_url_array_by_key("output", reference->value, output_segment_cardinality);
                    expand_url_array_by_key("output", reference->value, ontology, IoDirection::OUT);
                    relocate_url_array_by_key("output", reference->value, ontology, base);
                    list< URL > feed_url_array(decode_value_by_key< list< URL > >("output", reference->value));

                    for(auto& url : feed_url_array) {
                        ++(feed_resolution[url][index]);
                    }
                }
            }
            reference = value.FindMember("codec");
            if(reference != value.MemberEnd()) {
                if(!reference->value.IsNull()) {
                    if(reference->value.IsObject()) {
                        for(auto& record : reference->value.GetObject()) {
                            int32_t index(decode_value_by_key< int32_t >("index", record.value));
                            encode_key_value("TC", output_segment_cardinality, record.value, ontology);
                            pad_url_array_by_key("output", record.value, output_segment_cardinality);
                            expand_url_array_by_key("output", record.value, ontology, IoDirection::OUT);
                            relocate_url_array_by_key("output", record.value, ontology, base);
                            list< URL > feed_url_array(decode_value_by_key< list< URL > >("output", record.value));

                            for(auto& url : feed_url_array) {
                                ++(feed_resolution[url][index]);
                            }
                        }
                    }
                }
            }

            if(feed_resolution.size() > 0) {
                unordered_map< URL, Value > feed_ontology_by_url;
                int32_t index(0);
                for(const auto& url_record : feed_resolution) {
                    const URL& url = url_record.first;
                    int resolution(0);
                    for(const auto& record : url_record.second) {
                        if(resolution == 0) {
                            resolution = record.second;
                        } else if(resolution != record.second) {
                            throw ConfigurationError("inconsistent resolution for " + string(url));
                        }
                    }
                    Value proxy(kObjectType);
                    encode_key_value("index", index, proxy, ontology);
                    encode_key_value("url", url, proxy, ontology);
                    encode_key_value("direction", IoDirection::OUT, proxy, ontology);
                    encode_key_value("platform", platform, proxy, ontology);
                    encode_key_value("capacity", buffer_capacity * resolution, proxy, ontology);
                    encode_key_value("resolution", resolution, proxy, ontology);
                    encode_key_value("phred offset", phred_offset, proxy, ontology);
                    feed_ontology_by_url.emplace(make_pair(url, move(proxy)));
                    ++index;
                }

                reference = value.FindMember("undetermined");
                if(reference != value.MemberEnd()) {
                    if(!reference->value.IsNull()) {
                        if(reference->value.IsObject()) {
                            list< URL > feed_url_array(decode_value_by_key< list< URL > >("output", reference->value));
                            Value feed_by_segment(kArrayType);
                            for(const auto& url : feed_url_array) {
                                const Value& proxy(feed_ontology_by_url[url]);
                                feed_by_segment.PushBack(Value(proxy, ontology.GetAllocator()).Move(), ontology.GetAllocator());
                            }
                            reference->value.RemoveMember("feed by segment");
                            reference->value.AddMember("feed by segment", feed_by_segment.Move(), ontology.GetAllocator());
                        }
                    }
                }

                reference = value.FindMember("codec");
                if(reference != value.MemberEnd()) {
                    if(!reference->value.IsNull()) {
                        if(reference->value.IsObject()) {
                            for(auto& record : reference->value.GetObject()) {
                                list< URL > feed_url_array(decode_value_by_key< list< URL > >("output", record.value));
                                Value feed_by_segment(kArrayType);
                                for(const auto& url : feed_url_array) {
                                    const Value& proxy(feed_ontology_by_url[url]);
                                    feed_by_segment.PushBack(Value(proxy, ontology.GetAllocator()).Move(), ontology.GetAllocator());
                                }
                                record.value.RemoveMember("feed by segment");
                                record.value.AddMember("feed by segment", feed_by_segment.Move(), ontology.GetAllocator());
                            }
                        }
                    }
                }

                Value feed_array(kArrayType);
                for(auto& record : feed_ontology_by_url) {
                    feed_array.PushBack(record.second.Move(), ontology.GetAllocator());
                }
                ontology.RemoveMember("output feed");
                ontology.AddMember("output feed", feed_array.Move(), ontology.GetAllocator());
            }
            cross_validate_io();
        }
    }
};
void Demultiplex::compile_transformation(Value& value) {
    /* add the default observation if one was not specificed.
       default observation will treat every token as a segment */
    if(value.IsObject()) {
        Value::MemberIterator reference = value.FindMember("template");
        if(reference != value.MemberEnd()) {
            if(reference->value.IsObject()) {
                Value& template_element(reference->value);
                reference = template_element.FindMember("token");
                if(reference != template_element.MemberEnd()) {
                    if(reference->value.IsArray()) {
                        int32_t token_cardinality(reference->value.Size());

                        reference = template_element.FindMember("observation");
                        if(reference == template_element.MemberEnd() || reference->value.IsNull() || (reference->value.IsArray() && reference->value.Empty())) {
                            Value observation(kArrayType);
                            for(int32_t i(0); i < token_cardinality; ++i) {
                                string element(to_string(i));
                                observation.PushBack(Value(element.c_str(), element.size(),ontology.GetAllocator()).Move(), ontology.GetAllocator());
                            }
                            template_element.RemoveMember("observation");
                            template_element.AddMember(Value("observation", ontology.GetAllocator()).Move(), observation.Move(), ontology.GetAllocator());
                        }
                    } else { throw ConfigurationError("template token element is not an array"); }
                } else { throw ConfigurationError("template element is missing a token array"); }
            }
        }
    }
};
void Demultiplex::compile_decoder_codec(Value& value, const Value& default_instruction_decoder, const Value& default_instruction_codec) {
    if(value.IsObject()) {
        /* merge the default ontology decoder */
        merge_json_value(default_instruction_decoder, value, ontology);
        clean_json_value(value, ontology);

        /* compute default barcode induced by the codec */
        Value default_codec(kObjectType);
        project_json_value(default_instruction_codec, value, default_codec, ontology);

        double noise(decode_value_by_key< double >("noise", value));

        string buffer;
        int32_t barcode_index(0);
        double total_concentration(0);
        set< string > unique_barcode_id;

        /* apply barcode default on undetermined barcode or create it from the default if one was not explicitly specified */
        Value::MemberIterator reference = value.FindMember("undetermined");
        if(reference != value.MemberEnd()){
            merge_json_value(default_codec, reference->value, ontology);
            clean_json_value(reference->value, ontology);
        } else {
            value.AddMember (
                Value("undetermined", ontology.GetAllocator()).Move(),
                Value(default_codec, ontology.GetAllocator()).Move(),
                ontology.GetAllocator()
            );
        }

        reference = value.FindMember("undetermined");
        if(reference != value.MemberEnd()){
            encode_key_value("index", barcode_index, reference->value, ontology);
            if(infer_ID("ID", buffer, reference->value, true)) {
                unique_barcode_id.emplace(buffer);
            }
            encode_key_value("concentration", noise, reference->value, ontology);
            ++barcode_index;
        }

        reference = value.FindMember("codec");
        if(reference != value.MemberEnd()){
            if(reference->value.IsObject()) {
                Value& codec(reference->value);

                for(auto& record : codec.GetObject()) {
                    merge_json_value(default_codec, record.value, ontology);
                    clean_json_value(record.value, ontology);
                    encode_key_value("index", barcode_index, record.value, ontology);
                    if(infer_ID("ID", buffer, record.value)) {
                        if(!unique_barcode_id.count(buffer)) {
                            unique_barcode_id.emplace(buffer);
                        } else {
                            string duplicate(record.name.GetString(), record.value.GetStringLength());
                            throw ConfigurationError("duplicate " + duplicate + " barcode");
                        }
                    }

                    double concentration(decode_value_by_key< double >("concentration", record.value));
                    if(concentration >= 0) {
                        total_concentration += concentration;
                    } else { throw ConfigurationError("barcode concentration must be a positive number");  }

                    ++barcode_index;
                }

                if(total_concentration > 0) {
                    const double factor((1.0 - noise) / total_concentration);
                    for(auto& record : codec.GetObject()) {
                        double concentration(decode_value_by_key< double >("concentration", record.value));
                        encode_key_value("concentration", concentration * factor, record.value, ontology);
                    }
                } else { throw ConfigurationError("total pool concentration is not a positive number"); }
            } else { throw ConfigurationError("codec element must be a dictionary"); }
        }
        default_codec.SetNull();
    }
};
void Demultiplex::compile_decoder_transformation(Value& value) {
    compile_transformation(value);

    /* decode the transformation rule */
    Rule rule(decode_value_by_key< Rule >("template", value));
    int32_t input_segment_cardinality(decode_value_by_key< int32_t >("input segment cardinality", ontology));

    /* validate all tokens refer to an existing input segment */
    for(auto& token : rule.token_array) {
        if(!(token.input_segment_index < input_segment_cardinality)) {
            throw ConfigurationError("invalid input feed reference " + to_string(token.input_segment_index) + " in token " + to_string(token.index));
        }
    }

    /* annotate the decoder with cardinality information from the transfortmation */
    int32_t nucleotide_cardinality(0);
    vector< int32_t > barcode_length(rule.output_segment_cardinality, 0);
    for(auto& transform : rule.transform_array) {
        if(transform.token.constant()) {
            if(!transform.token.empty()) {
                barcode_length[transform.output_segment_index] += transform.token.length();
                nucleotide_cardinality += transform.token.length();
            } else { throw ConfigurationError("multiplex barcode token " + string(transform.token) + " is empty"); }
        } else { throw ConfigurationError("barcode token " + string(transform.token) + " is not fixed width"); }
    }
    encode_key_value("segment cardinality", rule.output_segment_cardinality, value, ontology);
    encode_key_value("nucleotide cardinality", nucleotide_cardinality, value, ontology);
    encode_key_value("barcode length", barcode_length, value, ontology);

    /* annotate each barcode element with the barcode segment cardinality */
    Value::MemberIterator reference = value.FindMember("undetermined");
    if(reference != value.MemberEnd()) {
        if(!reference->value.IsNull()) {
            Value& undetermined(reference->value);

            /* explicitly define a null barcode segment for the right dimension in the undetermined */
            Value barcode_segment(kArrayType);
            for(size_t i = 0; i < barcode_length.size(); ++i) {
                string sequence(barcode_length[i], '=');
                barcode_segment.PushBack(Value(sequence.c_str(), sequence.size(), ontology.GetAllocator()).Move(), ontology.GetAllocator());
            }
            undetermined.RemoveMember("barcode segment");
            undetermined.AddMember(Value("barcode segment", ontology.GetAllocator()).Move(), barcode_segment.Move(), ontology.GetAllocator());
            encode_key_value("segment cardinality", rule.output_segment_cardinality, undetermined, ontology);
        }
    }

    reference = value.FindMember("codec");
    if(reference != value.MemberEnd()) {
        if(!reference->value.IsNull()) {
            if(reference->value.IsObject()) {
                for(auto& record : reference->value.GetObject()) {
                    encode_key_value("segment cardinality", rule.output_segment_cardinality, record.value, ontology);
                }
            }
        }
    }
};
bool Demultiplex::infer_PU(const Value::Ch* key, string& buffer, Value& container, const bool& undetermined) {
    string suffix;
    if(!decode_value_by_key< string >(key, suffix, container)) {
        if(!undetermined) {
            list< string > barcode;
            if(decode_value_by_key< list< string > >("barcode segment", barcode, container)) {
                for(auto& segment : barcode) {
                    suffix.append(segment);
                }
            }
        } else { suffix.assign("undetermined"); }

        if(!suffix.empty()) {
            if(decode_value_by_key< string >("flowcell id", buffer, container)) {
                buffer.push_back(':');
                int32_t lane;
                if(decode_value_by_key< int32_t >("flowcell lane number", lane, container)) {
                    buffer.append(to_string(lane));
                    buffer.push_back(':');
                }
            }
            buffer.append(suffix);
            encode_key_value(key, buffer, container, ontology);
            return true;
        } else { return false; }
    } else { return true; }
};
bool Demultiplex::infer_ID(const Value::Ch* key, string& buffer, Value& container, const bool& undetermined) {
    if(!decode_value_by_key< string >(key, buffer, container)) {
        if(infer_PU("PU", buffer, container, undetermined)) {
            encode_key_value(key, buffer, container, ontology);
            return true;
        } else { return false; }
    } else { return true; }
};
void Demultiplex::pad_url_array_by_key(const Value::Ch* key, Value& container, const int32_t& cardinality) {
    list< URL > array;
    if(decode_value_by_key< list< URL > >(key, array, container)) {
        if(!array.empty()) {
            if(static_cast< int32_t >(array.size()) != cardinality) {
                if(array.size() == 1) {
                    while(static_cast< int32_t >(array.size()) < cardinality) {
                        array.push_back(array.front());
                    }
                    encode_key_value(key, array, container, ontology);
                } else { throw ConfigurationError("incorrect number of output URLs in channel"); }
            }
        }
    }
};
void Demultiplex::cross_validate_io() {
    list< URL > input_array(decode_value_by_key< list< URL > >("input", ontology));

    set< URL > input;
    for(auto& url : input_array) {
        input.emplace(url);
    }

    Value::MemberIterator reference = ontology.FindMember("multiplex");
    if(reference != ontology.MemberEnd()) {
        if(reference->value.IsObject()) {
            reference = reference->value.FindMember("codec");
            if(reference != ontology.MemberEnd()) {
                if(reference->value.IsObject()) {
                    for(auto& record : reference->value.GetObject()) {
                        list< URL > output;
                        if(decode_value_by_key< list< URL > >("output", output, record.value)) {
                            for(auto& url : output) {
                                if(input.count(url) > 0) {
                                    throw ConfigurationError("URL " + string(url) + " is used for both input and output");
                                }
                            }
                        }
                    }
                }
            }
        }
    }
};
void Demultiplex::validate_codec_group_sanity(const Value::Ch* key) {
    Value::MemberIterator reference = ontology.FindMember(key);
    if(reference != ontology.MemberEnd()) {
        if(!reference->value.IsNull()) {
            if(reference->value.IsObject()) {
                validate_codec_sanity(reference->value);
            } else if(reference->value.IsArray()) {
                for(auto& decoder : reference->value.GetArray()) {
                    if(!decoder.IsNull()) {
                        validate_codec_sanity(decoder);
                    }
                }
            }
        }
    }
};
void Demultiplex::validate_codec_sanity(Value& value) {
    if(!value.IsNull()) {
        CodecMetric metric(value);
        if(!metric.empty()) {
            metric.apply_barcode_tolerance(value, ontology);
        }
        double confidence_threshold;
        if(decode_value_by_key< double >("confidence threshold", confidence_threshold, value)) {
            if(confidence_threshold < 0 || confidence_threshold > 1) {
                throw ConfigurationError("confidence threshold value " + to_string(confidence_threshold) + " not between 0 and 1");
            }
        }

        double noise;
        if(decode_value_by_key< double >("noise", noise, value)) {
            if(noise < 0 || noise > 1) {
                throw ConfigurationError("noise value " + to_string(noise) + " not between 0 and 1");
            }
        }
    }
};
void Demultiplex::validate_url_accessibility() {
    URL url;
    Value::MemberIterator reference = ontology.FindMember("input feed");
    if(reference != ontology.MemberEnd()) {
        for(auto& element : reference->value.GetArray()) {
            if(decode_value_by_key< URL >("url", url, element)) {
                if(!url.is_readable()) {
                    throw IOError("could not open " + string(url) + " for reading");
                }
            }
        }
    }

    reference = ontology.FindMember("output feed");
    if(reference != ontology.MemberEnd()) {
        for(auto& element : reference->value.GetArray()) {
            if(decode_value_by_key< URL >("url", url, element)) {
                if(!url.is_writable()) {
                    throw IOError("could not open " + string(url) + " for writing");
                }
            }
        }
    }
};
void Demultiplex::load_thread_pool() {
    int32_t threads(decode_value_by_key< int32_t >("threads", ontology));
    thread_pool.pool = hts_tpool_init(threads);
    if(!thread_pool.pool) { throw InternalError("error creating thread pool"); }
};
void Demultiplex::load_input() {
    /*  Decode feed_proxy_array, a local list of input feed proxy.
        The list has already been enumerated by the interface
        and contains only unique url references
    */
    list< FeedProxy > feed_proxy_array(decode_value_by_key< list< FeedProxy > >("input feed", ontology));

    /*  Initialized the hfile reference and verify input format */
    for(auto& proxy : feed_proxy_array) {
        proxy.probe();
    };

    /*  Load feed_by_url, a local map of input feeds by url, from the proxy.
        Populate input_feed_by_index used to enumerate threaded access to the input feeds */
    unordered_map< URL, Feed* > feed_by_url(feed_proxy_array.size());
    for(auto& proxy : feed_proxy_array) {
        Feed* feed(NULL);
        switch(proxy.kind()) {
            case FormatKind::FASTQ: {
                feed = new FastqFeed(proxy);
                break;
            };
            case FormatKind::HTS: {
                feed = new HtsFeed(proxy);
                break;
            };
            case FormatKind::DEV_NULL: {
                feed = new NullFeed(proxy);
                break;
            };
            default: {
                throw InternalError("unknown input format " + string(proxy.url));
                break;
            };
        }
        feed->set_thread_pool(&thread_pool);
        input_feed_by_index.push_back(feed);
        feed_by_url.emplace(make_pair(proxy.url, feed));
    }

    list< URL > url_by_segment(decode_value_by_key< list < URL > >("input", ontology));

    /*  Populate the input_feed_by_segment array */
    input_feed_by_segment.reserve(url_by_segment.size());
    for(auto& url : url_by_segment) {
        const auto& record = feed_by_url.find(url);
        if(record != feed_by_url.end()) {
            input_feed_by_segment.push_back(record->second);
        } else {
            throw InternalError("missing feed for URL " + string(url) + " referenced in input proxy segment array");
        }
    }
};
void Demultiplex::load_output() {
    /*  Decode feed_proxy_array, a local list of output feed proxy.
        The list has already been enumerated by the environment
        and contains only unique url references
    */
    list< FeedProxy > feed_proxy_array(decode_value_by_key< list< FeedProxy > >("output feed", ontology));
    HeadPGAtom program(decode_value_by_key< HeadPGAtom >("program", ontology));

    /*  Register the read group elements on the feed proxy so it can be added to SAM header
        if a URL is present in the channel output that means the channel writes output to that file
        and the read group should be added to the header of that file.
    */
    map< URL, FeedProxy* > feed_proxy_by_url;
    for(auto& proxy : feed_proxy_array) {
        proxy.register_pg(program);
        feed_proxy_by_url.emplace(make_pair(proxy.url, &proxy));
    };

    Value::ConstMemberIterator reference = ontology.FindMember("multiplex");
    if(reference != ontology.MemberEnd()) {
        if(!reference->value.IsNull()) {
            if(reference->value.IsObject()) {
                const Value& multiplex(reference->value);
                reference = multiplex.FindMember("codec");
                if(reference != ontology.MemberEnd()) {
                    for(auto& record : reference->value.GetObject()) {
                        HeadRGAtom rg(record.value);
                        list< URL > output(decode_value_by_key< list< URL > >("output", record.value));
                        for(auto& url : output) {
                            feed_proxy_by_url[url]->register_rg(rg);
                        }
                    }
                }
            } else { throw ConfigurationError("multiplex element must be a dictionary"); }
        }
    }

    /*  Initialized the hfile reference */
    for(auto& proxy : feed_proxy_array) {
        proxy.probe();
    };

    /*  Load feed_by_url, a local map of output feeds by url, from the proxy.
        Populate output_feed_by_index used to enumerate threaded access to the output feeds */
    output_feed_by_url.reserve(feed_proxy_array.size());
    for(auto& proxy : feed_proxy_array) {
            Feed* feed(NULL);
            switch(proxy.kind()) {
                case FormatKind::FASTQ: {
                    feed = new FastqFeed(proxy);
                    break;
                };
                case FormatKind::HTS: {
                    feed = new HtsFeed(proxy);
                    break;
                };
                case FormatKind::DEV_NULL: {
                    feed = new NullFeed(proxy);
                    break;
                };
                default: {
                    throw InternalError("unknown output format " + string(proxy.url));
                    break;
                };
            }
            feed->set_thread_pool(&thread_pool);
            output_feed_by_index.push_back(feed);
            output_feed_by_url.emplace(make_pair(proxy.url, feed));
    }
};
void Demultiplex::load_pivot() {
    int32_t threads(decode_value_by_key< int32_t >("threads", ontology));
    for(int32_t index(0); index < threads; ++index) {
        pivot_array.emplace_back(*this, index);
    }
};
void Demultiplex::populate_decoder(DiscreteDecoder< Channel >& decoder) {
    populate_channel(decoder.undetermined);
    for(auto& channel : decoder.element_by_index) {
        populate_channel(channel);
    }
};
void Demultiplex::populate_channel(Channel& channel) {
    map< int32_t, Feed* > feed_by_index;
    channel.output_feed_by_segment.reserve(channel.output_feed_url_by_segment.size());
    for(const auto& url : channel.output_feed_url_by_segment) {
        Feed* feed(output_feed_by_url[url]);
        channel.output_feed_by_segment.emplace_back(feed);
        if(feed_by_index.count(feed->index) == 0) {
            feed_by_index.emplace(make_pair(feed->index, feed));
        }
    }
    channel.output_feed_by_segment.shrink_to_fit();

    channel.output_feed_lock_order.reserve(feed_by_index.size());
    for(auto& record : feed_by_index) {
        /* /dev/null is not really being written to so we don't need to lock it */
        if(!record.second->is_dev_null()) {
            channel.output_feed_lock_order.push_back(record.second);
        }
    }
    channel.output_feed_lock_order.shrink_to_fit();
};
void Demultiplex::print_global_instruction(ostream& o) const {
    o << setprecision(16);
    o << "Environment " << endl << endl;
    // o << "    Version                                     " << interface.application_version << endl;

    URL base_input_url;
    decode_value_by_key< URL >("base input url", base_input_url, ontology);
    o << "    Base input URL                              " << base_input_url << endl;

    URL base_output_url;
    decode_value_by_key< URL >("base input url", base_output_url, ontology);
    o << "    Base output URL                             " << base_output_url << endl;

    Platform platform;
    decode_value_by_key< Platform >("platform", platform, ontology);
    o << "    Platform                                    " << platform << endl;

    bool disable_quality_control;
    decode_value_by_key< bool >("disable quality control", disable_quality_control, ontology);
    o << "    Quality tracking                            " << (disable_quality_control ? "disabled" : "enabled") << endl;

    bool include_filtered;
    decode_value_by_key< bool >("include filtered", include_filtered, ontology);
    o << "    Include non PF reads                        " << (include_filtered ? "enabled" : "disabled") << endl;

    uint8_t input_phred_offset;
    decode_value_by_key< uint8_t >("input phred offset", input_phred_offset, ontology);
    o << "    Input Phred offset                          " << to_string(input_phred_offset) << endl;

    uint8_t output_phred_offset;
    decode_value_by_key< uint8_t >("output phred offset", output_phred_offset, ontology);
    o << "    Output Phred offset                         " << to_string(output_phred_offset) << endl;

    int32_t leading_segment_index;
    decode_value_by_key< int32_t >("leading segment index", leading_segment_index, ontology);
    o << "    Leading template segment                    " << to_string(leading_segment_index) << endl;

    int32_t buffer_capacity;
    decode_value_by_key< int32_t >("buffer capacity", buffer_capacity, ontology);
    o << "    Feed buffer capacity                        " << to_string(buffer_capacity) << endl;

    int32_t threads;
    decode_value_by_key< int32_t >("threads", threads, ontology);
    o << "    Threads                                     " << to_string(threads) << endl;
    o << endl;
};
void Demultiplex::print_codec_group_instruction(const Value::Ch* key, const string& head, ostream& o) const {
    Value::ConstMemberIterator reference = ontology.FindMember(key);
    if(reference != ontology.MemberEnd()) {
        if(!reference->value.IsNull()) {
            o << head << endl << endl;
            if(reference->value.IsObject()) {
                print_codec_instruction(reference->value, false, o);
            } else if(reference->value.IsArray()) {
                bool plural(reference->value.Size() > 1);
                for(auto& decoder : reference->value.GetArray()) {
                    if(!decoder.IsNull()) {
                        print_codec_instruction(decoder, plural, o);
                    }
                }
            }
        }
    }
};
void Demultiplex::print_codec_instruction(const Value& value, const bool& plural, ostream& o) const {
    if(!value.IsNull()) {
        if(plural) {
            int32_t index;
            if(decode_value_by_key< int32_t >("index", index, value)) {
                o << "  Decoder No." << to_string(index) << endl << endl;
            }
        }

        Algorithm algorithm(decode_value_by_key< Algorithm >("algorithm", value));
        o << "    Decoding algorithm                   " << algorithm << endl;

        uint8_t quality_masking_threshold;
        if(decode_value_by_key< uint8_t >("quality masking threshold", quality_masking_threshold, value)) {
            if(quality_masking_threshold > 0) {
                o << "    Quality masking threshold            " << int32_t(quality_masking_threshold) << endl;
            }
        }

        if(algorithm == Algorithm::MDD) {
            vector< uint8_t > distance_tolerance;
            if(decode_value_by_key< vector< uint8_t > >("distance tolerance", distance_tolerance, value)) {
                o << "    Distance tolerance                  ";
                for(auto& element : distance_tolerance) {
                    o << " " << int32_t(element);
                }
                o << endl;
            }
        }

        if(algorithm == Algorithm::PAMLD) {
            double noise(decode_value_by_key< double >("noise", value));
            o << "    Noise                                " << noise << endl;

            double confidence_threshold(decode_value_by_key< double >("confidence threshold", value));
            o << "    Confidence threshold                 " << confidence_threshold << endl;
        }

        print_codec_template(value, o);
        if(display_distance()) {
            CodecMetric metric(value);
            metric.describe(cout);
        }

        Value::ConstMemberIterator reference = value.FindMember("undetermined");
        if(reference != value.MemberEnd()) {
            print_channel_instruction(reference->value, o);
        }
        reference = value.FindMember("codec");
        if(reference != value.MemberEnd()) {
            if(reference->value.IsObject()) {
                for(const auto& record : reference->value.GetObject()) {
                    print_channel_instruction(record.value, o);
                }
            }
        }
    }
};
void Demultiplex::print_channel_instruction(const Value& value, ostream& o) const {
    if(value.IsObject()) {
        int32_t index(decode_value_by_key< int32_t >("index", value));
        o << "    Channel No." << index << endl;

        string buffer;
        if(decode_value_by_key< string >("ID", buffer, value)) { o << "        ID : " << buffer << endl; }
        if(decode_value_by_key< string >("PU", buffer, value)) { o << "        PU : " << buffer << endl; }
        if(decode_value_by_key< string >("LB", buffer, value)) { o << "        LB : " << buffer << endl; }
        if(decode_value_by_key< string >("SM", buffer, value)) { o << "        SM : " << buffer << endl; }
        if(decode_value_by_key< string >("DS", buffer, value)) { o << "        DS : " << buffer << endl; }
        if(decode_value_by_key< string >("DT", buffer, value)) { o << "        DT : " << buffer << endl; }
        if(decode_value_by_key< string >("PL", buffer, value)) { o << "        PL : " << buffer << endl; }
        if(decode_value_by_key< string >("PM", buffer, value)) { o << "        PM : " << buffer << endl; }
        if(decode_value_by_key< string >("CN", buffer, value)) { o << "        CN : " << buffer << endl; }
        if(decode_value_by_key< string >("FO", buffer, value)) { o << "        FO : " << buffer << endl; }
        if(decode_value_by_key< string >("KS", buffer, value)) { o << "        KS : " << buffer << endl; }
        if(decode_value_by_key< string >("PI", buffer, value)) { o << "        PI : " << buffer << endl; }
        if(decode_value_by_key< string >("FS", buffer, value)) { o << "        FS : " << buffer << endl; }
        if(decode_value_by_key< string >("CO", buffer, value)) { o << "        CO : " << buffer << endl; }
        double concentration;
        if(decode_value_by_key< double >("concentration", concentration, value)) {
            o << "        Concentration : " << concentration << endl;
        }

        Barcode barcode(value);
        if(!barcode.empty()) { o << "        Barcode       : " << barcode.iupac_ambiguity() << endl; }

        int32_t segment_index(0);
        list< URL > output;
        if(decode_value_by_key< list< URL > >("output", output, value)) {
            for(auto& url : output) {
                o << "        Segment No." + to_string(segment_index) + "  : " << url << endl;
                ++segment_index;
            }
        }
        o << endl;
    }
};
void Demultiplex::print_feed_instruction(const Value::Ch* key, ostream& o) const {
    Value::ConstMemberIterator reference = ontology.FindMember(key);
    if(reference != ontology.MemberEnd()) {
        if(!reference->value.IsNull()) {
            if(reference->value.IsArray()) {
                if(!reference->value.Empty()) {
                    for(const auto& element : reference->value.GetArray()) {
                        IoDirection direction(decode_value_by_key< IoDirection >("direction", element));
                        int32_t index(decode_value_by_key< int32_t >("index", element));
                        int32_t resolution(decode_value_by_key< int32_t >("resolution", element));
                        int32_t capacity(decode_value_by_key< int32_t >("capacity", element));
                        URL url(decode_value_by_key< URL >("url", element));
                        Platform platform(decode_value_by_key< Platform >("platform", element));
                        uint8_t phred_offset(decode_value_by_key< uint8_t >("phred offset", element));

                        o << "    ";
                        switch (direction) {
                            case IoDirection::IN:
                                o << "Input";
                                break;
                            case IoDirection::OUT:
                                o << "Output";
                                break;
                            default:
                                break;
                        }
                        o << " feed No." << index << endl;
                        o << "        Type : " << url.type() << endl;
                        o << "        Resolution : " << resolution << endl;
                        o << "        Phred offset : " << to_string(phred_offset) << endl;
                        o << "        Platform : " << platform << endl;
                        o << "        Buffer capacity : " << capacity << endl;
                        o << "        URL : " << url << endl;
                        o << endl;
                    }
                }
            }
        }
    }
};
void Demultiplex::print_input_instruction(ostream& o) const {
    o << "Input " << endl << endl;

    int32_t input_segment_cardinality;
    if(decode_value_by_key< int32_t >("input segment cardinality", input_segment_cardinality, ontology)) {
        o << "    Input segment cardinality                   " << to_string(input_segment_cardinality) << endl;
    }

    list< URL > input_url_array;
    if(decode_value_by_key< list< URL > >("input", input_url_array, ontology)) {
        o << endl;
        int32_t url_index(0);
        for(auto& url : input_url_array) {
            o << "    Input segment No." << url_index << " : " << url << endl;
            ++url_index;
        }
        o << endl;
    }
    print_feed_instruction("input feed", o);
};
void Demultiplex::print_template_instruction(ostream& o) const {
    o << "Template" << endl << endl;

    int32_t output_segment_cardinality;
    if(decode_value_by_key< int32_t >("output segment cardinality", output_segment_cardinality, ontology)) {
        o << "    Output segment cardinality                  " << to_string(output_segment_cardinality) << endl;
    }

    Rule template_rule(decode_value_by_key< Rule >("template", ontology));
    o << endl;
    for(auto& token : template_rule.token_array) {
        o << "    Token No." << token.index << endl;
        o << "        Length        " << (token.constant() ? to_string(token.length()) : "variable") << endl;
        o << "        Pattern       " << string(token) << endl;
        o << "        Description   ";
        o << token.description() << endl;
        o << endl;
    }
    o << "    Transformation" << endl;
    for(const auto& transform : template_rule.transform_array) {
        o << "        " << transform.description() << endl;
    }
    o << endl;
};
void Demultiplex::print_codec_template(const Value& value, ostream& o) const {
    int32_t segment_cardinality;
    if(decode_value_by_key< int32_t >("segment cardinality", segment_cardinality, value)) {
        o << "    Segment cardinality                  " << to_string(segment_cardinality) << endl;
    }

    int32_t nucleotide_cardinality;
    if(decode_value_by_key< int32_t >("nucleotide cardinality", nucleotide_cardinality, value)) {
        o << "    Nucleotide cardinality               " << to_string(nucleotide_cardinality) << endl;
    }

    if(segment_cardinality > 1) {
        vector< int32_t > barcode_length;
        if(decode_value_by_key< vector< int32_t > >("barcode length", barcode_length, value)) {
            o << "    Barcode segment length               ";
            for(const auto& v : barcode_length) {
                o << to_string(v) << " ";
            }
            o << endl;
        }
    }

    Rule rule(decode_value_by_key< Rule >("template", value));
    o << endl;
    for(auto& token : rule.token_array) {
        o << "    Token No." << token.index << endl;
        o << "        Length        " << (token.constant() ? to_string(token.length()) : "variable") << endl;
        o << "        Pattern       " << string(token) << endl;
        o << "        Description   ";
        o << token.description() << endl;
        o << endl;
    }
    o << "    Transform" << endl;
    for(const auto& transform : rule.transform_array) {
        o << "        " << transform.description() << endl;
    }
    o << endl;
};
void Demultiplex::print_multiplex_instruction(ostream& o) const {
    print_codec_group_instruction("multiplex", "Mutliplexing", o);
    print_feed_instruction("output feed", o);
};
void Demultiplex::print_molecular_instruction(ostream& o) const {
    print_codec_group_instruction("molecular", "Unique Molecular Identifier", o);
};
void Demultiplex::print_splitseq_instruction(ostream& o) const {
    print_codec_group_instruction("splitseq", "SplitSEQ", o);
};

DemultiplexPivot::DemultiplexPivot(Demultiplex& job, const int32_t& index) :
    index(index),
    platform(decode_value_by_key< Platform >("platform", job.ontology)),
    leading_segment_index(decode_value_by_key< int32_t >("leading segment index", job.ontology)),
    input_segment_cardinality(decode_value_by_key< int32_t >("input segment cardinality", job.ontology)),
    output_segment_cardinality(decode_value_by_key< int32_t >("output segment cardinality", job.ontology)),
    input(input_segment_cardinality, platform, leading_segment_index),
    output(output_segment_cardinality, platform, leading_segment_index),
    multiplex(NULL),
    input_accumulator(job.ontology),
    output_accumulator(find_value_by_key("multiplex", job.ontology)),
    job(job),
    disable_quality_control(decode_value_by_key< bool >("disable quality control", job.ontology)),
    template_rule(decode_value_by_key< Rule >("template", job.ontology)) {

    load_multiplex_decoder();
    load_molecular_decoder();
    load_splitseq_decoder();
    clear();
};
void DemultiplexPivot::load_multiplex_decoder() {
    Value::ConstMemberIterator reference = job.ontology.FindMember("multiplex");
    if(reference != job.ontology.MemberEnd()) {
        if(!reference->value.IsNull()) {
            if(reference->value.IsObject()) {
                Algorithm algorithm(decode_value_by_key< Algorithm >("algorithm", reference->value));
                BarcodeDecoder< Channel >* decoder;
                switch (algorithm) {
                    case Algorithm::PAMLD: {
                        decoder = new PAMLMultiplexDecoder(reference->value);
                        break;
                    };
                    case Algorithm::MDD: {
                        decoder = new MDMultiplexDecoder(reference->value);
                        break;
                    };
                    default:
                        throw ConfigurationError("unknown multiplex decoder algorithm");
                        break;
                }
                job.populate_decoder(*decoder);
                multiplex = decoder;
            } else { throw ConfigurationError("multiplex element must be a dictionary"); }
        }
    }
};
void DemultiplexPivot::load_molecular_decoder() {
    Value::ConstMemberIterator reference = job.ontology.FindMember("molecular");
    if(reference != job.ontology.MemberEnd()) {
        if(!reference->value.IsNull()) {
            if(reference->value.IsArray()) {
                molecular.reserve(reference->value.Size());
                for(const auto& element : reference->value.GetArray()) {
                    if(element.IsObject()) {
                        Algorithm algorithm(decode_value_by_key< Algorithm >("algorithm", element));
                        switch (algorithm) {
                            case Algorithm::SIMPLE: {
                                molecular.emplace_back(new SimpleMolecularDecoder(element));
                                break;
                            };
                            default:
                                throw ConfigurationError("unknown molecular decoder algorithm");
                                break;
                        }
                    } else { throw ConfigurationError("molecular decoder array element must be a dictionary"); }


                }
            } else { throw ConfigurationError("molecular decoder element must be an array"); }
        }
    }
};
void DemultiplexPivot::load_splitseq_decoder() {
    Value::ConstMemberIterator reference = job.ontology.FindMember("splitseq");
    if(reference != job.ontology.MemberEnd()) {
        if(!reference->value.IsNull()) {
            if(reference->value.IsArray()) {
                splitseq.reserve(reference->value.Size());
                for(const auto& element : reference->value.GetArray()) {
                    if(element.IsObject()) {
                        Algorithm algorithm(decode_value_by_key< Algorithm >("algorithm", element));
                        switch (algorithm) {
                            case Algorithm::PAMLD: {
                                splitseq.emplace_back(new PAMLSplitSEQDecoder(element));
                                break;
                            };
                            case Algorithm::MDD: {
                                splitseq.emplace_back(new MDSplitSEQDecoder(element));
                                break;
                            };
                            default:
                                throw ConfigurationError("unknown SplitSEQ decoder algorithm");
                                break;
                        }
                    } else { throw ConfigurationError("SplitSEQ decoder array element must be a dictionary"); }
                }
            } else { throw ConfigurationError("SplitSEQ decoder element must be an array"); }
        }
    }
};
void DemultiplexPivot::run() {
    while(job.pull(input)) {
        input.validate();
        transform();
        push();
        increment();
        clear();
    }
};