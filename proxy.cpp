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

#include "proxy.h"

void to_string(const FormatKind& value, string& result) {
    switch(value) {
        case FormatKind::FASTQ:         result.assign("FASTQ");      break;
        case FormatKind::HTS:           result.assign("HTS");        break;
        case FormatKind::DEV_NULL:      result.assign("DEV_NULL");   break;
        default:                        result.assign("UNKNOWN");    break;
    }
};
bool from_string(const char* value, FormatKind& result) {
         if(value == NULL)              result = FormatKind::UNKNOWN;
    else if(!strcmp(value, "FASTQ"))    result = FormatKind::FASTQ;
    else if(!strcmp(value, "HTS"))      result = FormatKind::HTS;
    else if(!strcmp(value, "DEV_NULL"))      result = FormatKind::DEV_NULL;
    else                                result = FormatKind::UNKNOWN;

    return (result == FormatKind::UNKNOWN ? false : true);
};
void to_kstring(const FormatKind& value, kstring_t& result) {
    ks_clear(result);
    string string_value;
    to_string(value, string_value);
    ks_put_string(string_value.c_str(), string_value.size(), result);
};
bool from_string(const string& value, FormatKind& result) {
    return from_string(value.c_str(), result);
};
ostream& operator<<(ostream& o, const FormatKind& value) {
    string string_value;
    to_string(value, string_value);
    o << string_value;
    return o;
};
void encode_key_value(const string& key, const FormatKind& value, Value& container, Document& document) {
    string string_value;
    to_string(value, string_value);
    Value v(string_value.c_str(), string_value.length(), document.GetAllocator());
    Value k(key.c_str(), key.size(), document.GetAllocator());
    container.RemoveMember(key.c_str());
    container.AddMember(k.Move(), v.Move(), document.GetAllocator());
};
template<> bool decode_value_by_key< FormatKind >(const Value::Ch* key, FormatKind& value, const Value& container) {
    Value::ConstMemberIterator element = container.FindMember(key);
    if(element != container.MemberEnd() && !element->value.IsNull()) {
        if(element->value.IsString()) {
            return from_string(element->value.GetString(), value);
        } else { throw ConfigurationError(string(key) + " element must be a string"); }
    }
    return false;
};

FeedProxy::FeedProxy(const Value& ontology) try :
    index(decode_value_by_key< int32_t >("index", ontology)),
    url(decode_value_by_key< URL >("url", ontology)),
    direction(decode_value_by_key< IoDirection >("direction", ontology)),
    phred_offset(decode_value_by_key< uint8_t >("phred offset", ontology)),
    hfile(NULL),
    capacity(decode_value_by_key< int32_t >("capacity", ontology)),
    resolution(decode_value_by_key< int32_t >("resolution", ontology)),
    platform(decode_value_by_key< Platform >("platform", ontology)) {

    } catch(ConfigurationError& error) {
        throw ConfigurationError("FeedProxy :: " + error.message);

    } catch(exception& error) {
        throw InternalError("FeedProxy :: " + string(error.what()));
};

void FeedProxy::set_capacity(const int& capacity) {
    if(capacity != this->capacity) {
        int aligned(static_cast< int >(capacity / resolution) * resolution);
        if(aligned < capacity) {
            aligned += resolution;
        }
        this->capacity = aligned;
    }
};
void FeedProxy::set_resolution(const int& resolution) {
    if(resolution != this->resolution) {
        int aligned(static_cast< int >(capacity / resolution) * resolution);
        if(aligned < capacity) {
            aligned += resolution;
        }
        this->resolution = resolution;
        this->capacity = aligned;
    }
};
void FeedProxy::register_rg(const HeadRGAtom& rg) {
    string key(rg);
    auto record = read_group_by_id.find(key);
    if(record == read_group_by_id.end()) {
        read_group_by_id.emplace(make_pair(key, HeadRGAtom(rg)));
    }
};
void FeedProxy::register_pg(const HeadPGAtom& pg) {
    string key(pg);
    auto record = program_by_id.find(key);
    if(record == program_by_id.end()) {
        program_by_id.emplace(make_pair(key, HeadPGAtom(pg)));
    }
};
void FeedProxy::probe() {
    if(!is_dev_null()) {
        switch(direction) {
            case IoDirection::IN: {
                /*  Probe input file

                    Here you can potentially use hfile to probe the file
                    and verify file format and potentially examine the first read
                */
                hfile = hopen(url.c_str(), "r");
                if(url.type() == FormatType::UNKNOWN) {
                    ssize_t peeked(0);
                    unsigned char* buffer(NULL);
                    const ssize_t buffer_capacity(PEEK_BUFFER_CAPACITY);
                    if((buffer = static_cast< unsigned char* >(malloc(buffer_capacity))) == NULL) {
                        throw OutOfMemoryError();
                    }

                    /* detect input format with htslib and override the type encoded in the url */
                    htsFormat format;
                    if(!hts_detect_format(hfile, &format)) {
                        switch (format.format) {
                            case htsExactFormat::sam:
                                url.set_type(FormatType::SAM);
                                break;
                            case htsExactFormat::bam:
                                url.set_type(FormatType::BAM);
                                break;
                            case htsExactFormat::bai:
                                url.set_type(FormatType::BAI);
                                break;
                            case htsExactFormat::cram:
                                url.set_type(FormatType::CRAM);
                                break;
                            case htsExactFormat::crai:
                                url.set_type(FormatType::CRAI);
                                break;
                            case htsExactFormat::vcf:
                                url.set_type(FormatType::VCF);
                                break;
                            case htsExactFormat::bcf:
                                url.set_type(FormatType::BCF);
                                break;
                            case htsExactFormat::csi:
                                url.set_type(FormatType::CSI);
                                break;
                            case htsExactFormat::gzi:
                                url.set_type(FormatType::GZI);
                                break;
                            case htsExactFormat::tbi:
                                url.set_type(FormatType::TBI);
                                break;
                            case htsExactFormat::bed:
                                url.set_type(FormatType::BED);
                                break;
                            default:
                                url.set_type(FormatType::UNKNOWN);
                                break;
                        }
                    }

                    if(url.type() == FormatType::SAM) {
                        peeked = hpeek(hfile, buffer, buffer_capacity);
                        if(peeked > 0) {
                            switch (format.compression) {
                                case htsCompression::gzip:
                                case htsCompression::bgzf: {
                                    unsigned char* decompressed_buffer(NULL);
                                    if((decompressed_buffer = static_cast< unsigned char* >(malloc(buffer_capacity))) == NULL) {
                                        throw OutOfMemoryError();
                                    }
                                    z_stream zstream;
                                    zstream.zalloc = NULL;
                                    zstream.zfree = NULL;
                                    zstream.next_in = buffer;
                                    zstream.avail_in = static_cast< unsigned >(peeked);
                                    zstream.next_out = decompressed_buffer;
                                    zstream.avail_out = buffer_capacity;
                                    if(inflateInit2(&zstream, 31) == Z_OK) {
                                        while(zstream.total_out < buffer_capacity) {
                                            if(inflate(&zstream, Z_SYNC_FLUSH) != Z_OK) break;
                                        }
                                        inflateEnd(&zstream);
                                        memcpy(buffer, decompressed_buffer, zstream.total_out);
                                        peeked = zstream.total_out;
                                    } else {
                                        peeked = 0;
                                    }
                                    free(decompressed_buffer);
                                    break;
                                };
                                case htsCompression::no_compression:
                                    break;
                                default:
                                    throw InternalError("unknown compression");
                                    break;
                            }
                        }
                        if(peeked > 0) {
                            size_t state(0);
                            char* position(reinterpret_cast< char * >(buffer));
                            char* end(position + peeked);
                            while(position < end && position != NULL) {
                                if(state == 0) {
                                    if(*position == '\n') {
                                        ++position;
                                    } else {
                                        if(*position == '@') {
                                            state = 1;
                                        } else {
                                            break;
                                        }
                                    }
                                } else if(state == 1) {
                                    if((*position >= 'A' && *position <= 'Z') || (*position >= 'a' && *position <= 'z')) {
                                        state = 2;
                                    } else {
                                        break;
                                    }
                                } else if(state == 2) {
                                    if(*position == '+' && position < end && *(position + 1) == '\n') {
                                        url.set_type(FormatType::FASTQ);
                                    }
                                    break;
                                }
                                if((position = strchr(position, '\n')) != NULL) ++position;
                            }
                        }
                    }
                    free(buffer);
                }
                break;
            };
            case IoDirection::OUT: {
                hfile = hopen(url.c_str(), "w");
                break;
            };
            default:
                break;
        }
    }
};

bool encode_value(const string& key, const FeedProxy& value, Value& container, Document& document) {
    if(container.IsObject()) {
        container.RemoveMember(key.c_str());
        Value element(kObjectType);
        encode_key_value("index", value.index, element, document);
        encode_key_value("url", value.url, element, document);
        encode_key_value("direction", value.direction, element, document);
        encode_key_value("platform", value.platform, element, document);
        encode_key_value("capacity", value.capacity, element, document);
        encode_key_value("resolution", value.resolution, element, document);
        encode_key_value("phred offset", value.phred_offset, element, document);
        container.AddMember(Value(key.c_str(), key.size(), document.GetAllocator()).Move(), element.Move(), document.GetAllocator());
        return true;
    } else { throw ConfigurationError(string(key) + " container is not a dictionary"); }
    return false;
};
ostream& operator<<(ostream& o, const FeedProxy& proxy) {
    o << "direction : " << proxy.direction << endl;
    o << "index : " << proxy.index << endl;
    o << "url : " << proxy.url << endl;
    o << "platform : " << proxy.platform << endl;
    o << "capacity : " << proxy.capacity << endl;
    o << "resolution : " << proxy.resolution << endl;
    o << "phred_offset : " << to_string(proxy.phred_offset) << endl;
    o << proxy.url.description();
    return o;
};
template<> FeedProxy decode_value< FeedProxy >(const Value& container) {
    if(container.IsObject()) {
        FeedProxy proxy(container);
        return proxy;
    } else { throw ConfigurationError("feed proxy element must be a dictionary"); }
};
template<> list< FeedProxy > decode_value_by_key< list< FeedProxy > >(const Value::Ch* key, const Value& container) {
    if(container.IsObject()) {
        Value::ConstMemberIterator reference = container.FindMember(key);
        if(reference != container.MemberEnd()) {
            if(!reference->value.IsNull()) {
                if(reference->value.IsArray()) {
                    list< FeedProxy > value;
                    if(!reference->value.Empty()) {
                        for(auto& element : reference->value.GetArray()) {
                            value.emplace_back(element);
                        }
                    }
                    return value;
                } else { throw ConfigurationError(string(key) + " is not an array"); }
            } else { throw ConfigurationError(string(key) + " is null"); }
        } else { throw ConfigurationError(string(key) + " not found"); }
    } else { throw ConfigurationError(string(key) + " container is not a dictionary"); }
};
