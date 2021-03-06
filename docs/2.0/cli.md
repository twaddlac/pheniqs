<!--
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
-->

<section id="navigation">
    <ul>
        <li><a                  href="/pheniqs/2.0/">Home</a></li>
        <li><a                  href="/pheniqs/2.0/tutorial.html">Tutorial</a></li>
        <li><a                  href="/pheniqs/2.0/install.html">Install</a></li>
        <li><a                  href="/pheniqs/2.0/build.html">Build</a></li>
        <li><a                  href="/pheniqs/2.0/workflow.html">Workflow</a></li>
        <li><a                  href="/pheniqs/2.0/glossary.html">Glossary</a></li>
        <li><a                  href="/pheniqs/2.0/manual.html">Manual</a></li>
        <li><a class="active"   href="/pheniqs/2.0/cli.html">CLI</a></li>
        <li><a class="github"   href="http://github.com/biosails/pheniqs">View on GitHub</a></li>
    </ul>
    <div class="clear" />
</section>

<section class="notice_2_0">Please excuse us as we update the documentation for the new Pheniqs 2.0 API.</section>

# The Pheniqs Command Line
{:.page-title}

# Command line parameters
Command line parameters, if specified, override their corresponding values provided in the configuration file.

# zsh completion
If you use [zsh](https://en.wikipedia.org/wiki/Z_shell) you may wish to install the bundled zsh command line completion script for a more interactive command line experience. It will interactively complete the command line arguments for you and makes learning the interface more intuitive. The zsh completion script, `_pheniqs`, is generated when building pheniqs with `make` but you can also generate it by invoking the corresponding `make` target `make _pheniqs` or with `pheniqs-tools.py` by executing `pheniqs-tools.py zsh configuration.json > _pheniqs`. The script should be placed in one of the folders referenced in your in your **fpath**.

# Global command line help

    pheniqs version 2.0.3-beta-70-g3b6cb6d7a727ca0ad09c1c663d137c5f2719b74f
    Lior Galanti < lior.galanti@nyu.edu >
    NYU Center for Genomics & Systems Biology 2018
    See manual at https://biosails.github.io/pheniqs

    Usage : pheniqs [-h] [--version] ACTION ...

    Optional:
      -h, --help    Show this help
      --version     Show program version

    available action
      demux      Demultiplex and report quality control
      quality    Report quality control

    This program comes with ABSOLUTELY NO WARRANTY. This is free software,
    and you are welcome to redistribute it under certain conditions.

# Demux sub command help

    pheniqs version 2.0.3-beta-70-g3b6cb6d7a727ca0ad09c1c663d137c5f2719b74f
    Lior Galanti < lior.galanti@nyu.edu >
    NYU Center for Genomics & Systems Biology 2018
    See manual at https://biosails.github.io/pheniqs

    Demultiplex and report quality control

    Usage : pheniqs demux [-h] [-i PATH]* [-o PATH]* [-c PATH] [-I URL] [-O URL]
                          [-V] [-C] [-D] [-p FLOAT] [-f] [-q] [-n FLOAT] [-l INT]
                          [-P CAPILLARY|LS454|ILLUMINA|SOLID|HELICOS|IONTORRENT|ONT|PACBIO] [-t INT]
                          [-B INT]

    Optional:
      -h, --help                          Show this help
      -i, --input PATH                    Path to input files
      -o, --output PATH                   Path to output files
      -c, --config PATH                   Path to configuration file
      -I, --base-input URL                Base input url
      -O, --base-output URL               Base output url
      -V, --validate                      Only validate configuration
      -C, --compile                       Only compile configuration file
      -D, --distance                      Display pairwise barcode distance
      -p, --multiplex-confidence FLOAT    Decoding multiplex confidence threshold
      -f, --filtered                      Include filtered reads
      -q, --quality                       Disable quality control
      -n, --multiplex-noise FLOAT         Multiplex noise prior probability
      -l, --leading INT                   Leading read segment
      -P, --platform STRING               Sequencing platform
      -t, --threads INT                   Thread pool size
      -B, --buffer INT                    Records per resolution in feed buffer

    To provide multiple paths to -i/--input and -o/--output repeat the flag before every path,
    i.e. `pheniqs demux -i first_in.fastq -i second_in.fastq -o first_out.fastq -o second_out.fastq`

    -i/--input defaults to /dev/stdin, -o/--output default to /dev/stdout and output format default to SAM.
    -I, --base-input and -O, --base-output default to the working directory.

# JSON validation

JSON can be a little picky about syntax and a good JSON linter can make identifying offending syntax much easier. Plenty of tools for validating JSON syntax are out there but a simple good and readily available linter is available with the python programing language.

You will find a small [JSON linting python script]({{ site.github.repository_url }}/blob/master/tool/json_lint.py) in the tool directory that is somewhat customized for the pheniqs config file.

For **python 2** use:

    python -c "import json,sys; print json.dumps(json.load(sys.stdin),sort_keys=True,ensure_ascii=False,indent=4).encode('utf8')"

or for **python 3**:

    python3 -c "import json,sys; print(json.dumps(json.load(sys.stdin),sort_keys=True,ensure_ascii=False,indent=4))"

You may alternatively set it up as an alias in your shell's profile by adding to your `.zshrc` or `.bashrc`:

    alias jsl="python -c \"import json,sys; print json.dumps(json.load(sys.stdin),sort_keys=True,ensure_ascii=False,indent=4).encode('utf8')\""

or

    alias jsl="python3 -c \"import json,sys; print(json.dumps(json.load(sys.stdin),sort_keys=True,ensure_ascii=False,indent=4))\""

and than invoke it simply by feeding it a JSON file on standard input:

    cat configuration.json|jsl

This will print an easy to read tabulated JSON to standard output and assist you with resolving any syntactical JSON violations.
