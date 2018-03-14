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

#ifndef PHENIQS_TRANSFORM_H
#define PHENIQS_TRANSFORM_H

#include <set>
#include <unordered_map>

#include <htslib/hts.h>
#include <htslib/hfile.h>
#include <htslib/kstring.h>

#include "error.h"
#include "json.h"
#include "constant.h"

using std::set;
using std::copy;
using std::hash;
using std::setw;
using std::endl;
using std::cerr;
using std::cout;
using std::fixed;
using std::size_t;
using std::string;
using std::vector;
using std::ostream;
using std::ifstream;
using std::ios_base;
using std::exception;
using std::to_string;
using std::make_pair;
using std::setprecision;
using std::unordered_map;

/* Transform
*/
class Token {
    friend ostream& operator<<(ostream& o, const Token& token);
    void operator=(Token const &) = delete;

    public:
        const size_t index;
        const size_t input_segment_index;

        Token(
            const size_t& index,
            const size_t& input_segment_index,
            const int32_t& start,
            const int32_t& end,
            const bool& end_terminated);
        Token(const Token& other);
        string description() const;
        inline size_t decode_end(const size_t& length) const {
            size_t value;
            if(end_terminated) {
                int32_t v = (end < 0 ? length + end : end);
                value = v < 0 ? 0 : v;
                if(value > length) {
                    value = length;
                }
            } else {
                value = length;
            }
            return value;
        };
        inline size_t decode_start(const size_t& length) const {
            size_t value;
            int32_t v = start < 0 ? length + start : start;
            value = v < 0 ? 0 : v;
            if(value > length) {
                value = length;
            }
            return value;
        };
        inline bool empty() const {
            return (end_terminated && start >= end) && ((start >= 0 && end >= 0) || (start < 0 && end < 0));
        };
        inline bool constant() const {
            if(end_terminated) {
               return (start >= 0 && end >= 0) || (start < 0 && end < 0);
            } else {
                return start < 0;
            }
        };
        int32_t length() const {
            if(constant()) {
                if(end_terminated) {
                    return empty() ? 0 : end - start;
                } else {
                    return -start;
                }
            } else {
                return -1;
            }
        };
        operator string() const;

    private:
        const int32_t start;
        const int32_t end;
        const bool end_terminated;
};
class Transform {
    friend ostream& operator<<(ostream& o, const Transform& transform);
    void operator=(Transform const &) = delete;

    public:
        const size_t index;
        const size_t output_segment_index;
        const Token token;
        const LeftTokenOperator left;

        Transform(const size_t& index, const Token& token, const size_t& output_segment_index, const LeftTokenOperator& left);
        Transform(const Transform& other);
        string description() const;
        operator string() const;
};

#endif /* PHENIQS_TRANSFORM_H */