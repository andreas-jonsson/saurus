# Types

number, boolean, string, local, global, promise, sequence, vector, hashmap,

function, native-function, native-pointer, native-data

# Variables

**_ARGS** : vector

**_VERSION** : string

**_G** : local

**rec** : function

# Functions

vector **(** number ... **)** : any | vector

hashmap **(** any ... **)** : any | hashmap

string **(** number ... **)** : string

**seq(** vector | hashmap | string **)** : sequence

**cons(** any any **)** : sequence

**first(** sequence **)** : any

**rest(** sequence **)** : any

**vector(** any ... **)** : vector

**hashmap(** any any ... **)** : hashmap

**apply(** function sequence **)** : any

**compile(** string string?**)** : function | string

**load(** string **)** : function

**type?(** any **)** : string

**cat(** any ... **)** : any

### Mutable

**unref(** local **)** : any

**local(** any **)** : local

**global(** hashmap | nil **)** : global

**set(** local any **)** : any

### Error Handling

**error(** string **)** : nil

**assert(** any string **)** : any

### I/O

**io.print(** any ... **)** : nil

**io.read(** string | nil **)** : string

**io.write(** string string **)** : number

**io.size(** string **)** : number | nil

**io.delete(** string **)** : boolean

### OS

**os.name** : string | nil

**os.time()** : number

**os.loadlib(** string string **)** : nil

**os.execute(** string **)** : string

### Threading

**process.async(** function | native-function ... **)** : promise

**process.sync(** global function | native-function ... **)** : hashmap | nil

**process.peak(** promise **)** : boolean

**process.force(** promise **)** : any

**process.sleep(** number **)** : number

**process.num_threads()** : number

**process.num_cores()** : number

### HTTP

**http.request(** string string hashmap string | nil **)** : vector | nil

```saurus
response = http.request("GET" "http://localhost/index.html" {} nil)
if response(1) == 200
    io.print(response(4))
else
    error(string.format("Error response: %i %s" response(1) response(2)))
```

### String

**string.string!(** any **)** : string

**string.number!(** string **)** : number

**string.format(** string ... **)** : string

**string.[find](https://github.com/cesanta/slre)(** string string **)** : sequence

### Sequence

**sequence.range(** number number **)** : sequence

**sequence.rseq(** sequence | hashmap | vector | string **)** : sequence

**sequence.list(** any ... **)** : sequence

**sequence.push(** vector any **)** : vector

**sequence.pop(** vector **)** : vector

**sequence.dissoc(** hashmap any **)** : hashmap

**sequence.assoc?(** hashmap any **)** : boolean

**sequence.assoc(** vector | hashmap any any **)** : vector | hashmap

**sequence.length(** vector | hashmap | string **)** : number

### Math

**math.pi** : number

**math.huge** : number

**math.random(** number number **)** : number

**math.seed(** number **)** : nil

**math.abs(** number **)** : number

**math.acos(** number **)** : number

**math.asin(** number **)** : number

**math.atan(** number number? **)** : number

**math.cos(** number **)** : number

**math.cosh(** number **)** : number

**math.ceil(** number **)** : number

**math.floor(** number **)** : number

**math.fmod(** number number **)** : number

**math.log(** number **)** : number

**math.log10(** number **)** : number

**math.sin(** number **)** : number

**math.sinh(** number **)** : number

**math.sqrt(** number **)** : number

**math.tan(** number **)** : number

**math.tanh(** number **)** : number

**math.modf(** number **)** : sequence

**math.frexp(** number **)** : sequence

**math.ldexp(** number number **)** : number

**math.min(** number ... **)** : number

**math.max(** number ... **)** : number

**math.deg(** number **)** : number

**math.rad(** number **)** : number

# Constructs

**(** < identifier(s) > **) ->** < expression(s) > **;**

< identifier > **->** < expression(s) > **;**

< identifier > **=** < expression >

### Keywords

**def** < identifier > **=** < expression >

**for** < identifier > **=** < expression > < expression >

**if** < expression > < expression >

**if** < expression > < expression > **else** < expression >

**do** < expression(s) > **;**

**include** string

### Constants

**true false nil**

### Logical Operators

**== ~ ~= & | < > <= >=**

**. : @ ..**

### Arithmetic Operators

**+ - * / % ^**

# Foreign Function Interface

**cinclude** string

**cdec** string

**cfun** string
