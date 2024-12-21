# Lama bytecode verifier

## Prerequisites

`lamac` available in PATH

## Usage

After `git clone`, please initialize all submodules:
```
git submodule update --init --recursive
```

`make` to build the interpreter `./rapidlama`

`./rapidlama <BYTECODE.bc>` to interpret a bytecode file.

`make regression` and `make regression-expressions`

## Performance

`make performance` On my machine:

Compiled `./Sort`
```
[HW4] /usr/bin/time -f "Sort\t%U" performance/Sort                                                                   main  ✭ ✱
Sort    1.12
[HW4] /usr/bin/time -f "Sort\t%U" performance/Sort                                                                   main  ✭ ✱
Sort    1.11
[HW4] /usr/bin/time -f "Sort\t%U" performance/Sort                                                                   main  ✭ ✱
Sort    1.09
```

`lamac -i`
```
[HW4] /usr/bin/time -f "Sort\t%U" lamac -i performance/Sort.lama < /dev/null                                         main  ✭ ✱
Sort    4.05
[HW4] /usr/bin/time -f "Sort\t%U" lamac -i performance/Sort.lama < /dev/null                                         main  ✭ ✱
Sort    4.01
[HW4] /usr/bin/time -f "Sort\t%U" lamac -i performance/Sort.lama < /dev/null                                         main  ✭ ✱
Sort    4.01
```

`lamac -s`
```
[HW4] /usr/bin/time -f "Sort\t%U" lamac -s performance/Sort.lama < /dev/null                                         main  ✭ ✱
Sort    1.50
[HW4] /usr/bin/time -f "Sort\t%U" lamac -s performance/Sort.lama < /dev/null                                         main  ✭ ✱
Sort    1.47
[HW4] /usr/bin/time -f "Sort\t%U" lamac -s performance/Sort.lama < /dev/null                                         main  ✭ ✱
Sort    1.46
```

`YAILama`
```
[HW4] /usr/bin/time -f "Sort\t%U" ../HW2/hw2/YAILama performance/Sort.bc                                             main  ✭ ✱
Sort    2.01
[HW4] /usr/bin/time -f "Sort\t%U" ../HW2/hw2/YAILama performance/Sort.bc                                             main  ✭ ✱
Sort    2.00
[HW4] /usr/bin/time -f "Sort\t%U" ../HW2/hw2/YAILama performance/Sort.bc                                             main  ✭ ✱
Sort    2.01
```

`rapidlama`
```
[HW4] /usr/bin/time -f "Sort\t%U" ./rapidlama performance/Sort.bc                                                    main  ✭ ✱
verification time: 00.000039391
interpretation time: 01.831025238
Sort    1.83
[HW4] /usr/bin/time -f "Sort\t%U" ./rapidlama performance/Sort.bc                                                    main  ✭ ✱
verification time: 00.000104483
interpretation time: 01.827249459
Sort    1.82
[HW4] /usr/bin/time -f "Sort\t%U" ./rapidlama performance/Sort.bc                                                    main  ✭ ✱
verification time: 00.000098477
interpretation time: 01.832083235
Sort    1.82
```


As we can see, verification time is negligible.
