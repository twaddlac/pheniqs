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
        <li><a                  href="/pheniqs/1.0/">Home</a></li>
        <li><a                  href="/pheniqs/1.0/tutorial.html">Tutorial</a></li>
        <li><a                  href="/pheniqs/1.0/manual.html">Documentation</a></li>
        <li><a                  href="/pheniqs/1.0/glossary.html">Glossary</a></li>
        <li><a                  href="/pheniqs/1.0/workflow.html">Workflow</a></li>
        <li><a                  href="/pheniqs/1.0/cli.html">Command line interface</a></li>
        <li><a class="active"   href="/pheniqs/1.0/building.html">Building</a></li>
        <li><a class="github"   href="http://github.com/biosails/pheniqs">View on GitHub</a></li>
    </ul>
    <div class="clear" />
</section>

# Building
{:.page-title}

Pheniqs is regularly tested to build with the supplied [Makefile]({{ site.github.repository_url }}/blob/master/Makefile) on MacOS and Ubuntu but should build on most POSIX systems that provide the dependencies. Pheniqs depends only on [HTSlib 1.4](https://github.com/samtools/htslib/releases/tag/1.4) and [RapidJSON 1.1.0](https://github.com/miloyip/rapidjson/releases/tag/v1.1.0).

## MacOS
Pheniqs has been tested to build on MacOS 10.12 with dependencies installed from [homebrew](http://brew.sh)

```zsh
brew install rapidjson htslib
```

## Ubuntu
HTSlib and RapidJSON are packaged in the community maintained universe repository as `libhts-dev` and `rapidjson-dev` respectively so theoretically you can install them with

```zsh
sudo apt-get install rapidjson-dev libhts-dev
```

Sadly the universe packaged versions are often very old and Pheniqs will fail to link against them. Both are however very easy to install from source.

To install HTSlib from source simply follow the [instructions](https://github.com/samtools/htslib/blob/develop/README.md) posted on the HTSlib repository.

RapidJSON is a header only implementation with no external dependencies so installing it boils down to copying the [RapidJSON include](https://github.com/miloyip/rapidjson/tree/master/include/rapidjson) directory to your include path which is usually `/usr/local/include`.

If you install either on a non standard prefix you can provide the Makefile with alternative prefix paths in the `environment.mk` file. See the [sample environment.mk]({{ site.github.repository_url }}/blob/master/environment.mk.default) file for details.