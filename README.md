# About

Saurus is a dynamic, lightweight, general purpose programming language. The VM is written in portable C and can be easily embedded in another C or C++ program. The language is simple, powerful and dynamic in a fashion familiar to most programmers.

![](https://raw.githubusercontent.com/andreas-t-jonsson/saurus/master/doc/saurus_logo_64.png) [Tweets by @saurus_org](https://twitter.com/saurus_org)

### Examples

A fibonacci function written in Saurus.

```saurus
# Fibonacci function
fibonacci = (n) ->
    if n < 3
        1
    else
        rec(n - 1) + rec(n - 2)
    ;

# Print 8 fibonacci numbers
for n = 1..8
    io.print(n "=" fibonacci(n))
```

```
> saurus fibo.su
1	=	1
2	=	1
3	=	2
4	=	3
5	=	5
6	=	8
7	=	13
8	=	21
```

Mix Saurus with C code.

```saurus
hello_from_saurus = () ->
    io.print("Hello from Saurus!")
    ;

hello_from_c = cfun '''
    puts("Hello from C!");
'''

hello_from_saurus()
hello_from_c()

cdec '''
    int main(int argc, char *argv[]) {
        su_state *s = su_init(NULL);
        su_libinit(s);

        ___saurus(s);
        su_call(s, 0, 0);

        su_close(s);
        return 0;
    }
'''
```

```
> saurus -c hello.su hello.c
> gcc -lsaurus hello.c -o hello
> ./hello
Hello from Saurus!
Hello from C!
```

Non blocking multithreading in Saurus.

```saurus
accounts = global({a = 0 b = 10})

# This function verify concurrency.
check = () ->
    g = @accounts
    assert(g.a + g.b == 10 "Race condition!")
    if g.b > 0
        rec()
    ;

# Transfer N amount from account B to account A.
transfer = (n) ->
    # This function could be called multiple times
    # with the same argument, on the same thread.
    mutate = (g) -> {
            a = g.a + n
            b = g.b - n
        }
        ;

    process.sync(accounts mutate)
    ;

# Spawn 4 separate threads to do the work.
process.async(check)
process.async(transfer 3)
process.async(transfer 5)
process.async(transfer 2)
```

```
> saurus fibo.su
```

*We recommend using [Atom](https://atom.io/) with the [language-saurus](https://atom.io/packages/language-saurus) package for writing Saurus code.*

# Support & Documentation
Our [wiki](https://github.com/andreas-t-jonsson/saurus/wiki) contains basic information about the standard-library and built-in functions. More sample code can be found [here](https://github.com/andreas-t-jonsson/saurus-samples). If you need help or have a question you can use the [issue tracker](https://github.com/andreas-t-jonsson/saurus/issues) and tag your issue with the lable **question**. For discussions we recommend joining our [![Join the chat at https://gitter.im/andreas-t-jonsson/saurus](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/andreas-t-jonsson/saurus?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

###### Current build status:

Linux (Clang): [![Build Status](https://travis-ci.org/andreas-t-jonsson/saurus.svg?branch=master)](https://travis-ci.org/andreas-t-jonsson/saurus)

Windows (MSVC): [![Build status](https://ci.appveyor.com/api/projects/status/k4gb3qymp0jl7j6f/branch/master?svg=true)](https://ci.appveyor.com/project/andreas-t-jonsson/saurus/branch/master)

### License

Saurus is released under the [MPL-2.0](http://mozilla.org/MPL/2.0/) licence. (Summarized by [tldrlegal.com](https://tldrlegal.com/license/mozilla-public-license-2.0-%28mpl-2%29).)

### Acknowledgement

[SLRE](https://github.com/cesanta/slre) (Cesanta Software)

[Clojure](http://clojure.org/) (Rich Hickey)

[Lua](http://www.lua.org/) (PUC-Rio)

[InstallBuilder](http://installbuilder.bitrock.com/) (BitRock)
