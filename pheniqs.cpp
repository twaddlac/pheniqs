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

#include "environment.h"

int main(int argc, char** argv) {
    int return_code(static_cast< int >(ProgramState::OK));

    try {
        Environment environment(argc, (const char**)argv);
        environment.execute();

    } catch(InternalError& error) {
        cerr << error.what() << endl;
        return_code = static_cast< int >(ProgramState::INTERNAL_ERROR);

    } catch(ConfigurationError& error) {
        cerr << error.what() << endl;
        return_code =  static_cast< int >(ProgramState::CONFIGURATION_ERROR);

    } catch(OutOfMemoryError& error) {
        cerr << error.what() << endl;
        return_code =  static_cast< int >(ProgramState::OUT_OF_MEMORY_ERROR);

    } catch(CommandLineError& error) {
        cerr << error.what() << endl;
        return_code =  static_cast< int >(ProgramState::COMMAND_LINE_ERROR);

    } catch(IOError& error) {
        cerr << error.what() << endl;
        return_code =  static_cast< int >(ProgramState::IO_ERROR);

    } catch(SequenceError& error) {
        cerr << error.what() << endl;
        return_code =  static_cast< int >(ProgramState::SEQUENCE_ERROR);

    } catch(OverflowError& error) {
        cerr << error.what() << endl;
        return_code =  static_cast< int >(ProgramState::OVERFLOW_ERROR);

    } catch(CorruptAuxiliaryError& error) {
        cerr << error.what() << endl;
        return_code =  static_cast< int >(ProgramState::CORRUPT_AUXILIARY_ERROR);

    } catch(exception& error) {
        cerr << error.what() << endl;
        return_code =  static_cast< int >(ProgramState::UNKNOWN_ERROR);

    }
    return return_code;
};
