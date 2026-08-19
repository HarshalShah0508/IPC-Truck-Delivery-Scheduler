# Helper and Testcase Generator

## Generating Testcases

You can modify the following params in `testcase_gen.py` to generate custom testcases:
N,D,S,T,B, max_new_requests_per_turn, number_of_requests, max_booth_cost and test_case_number

Once these have been set, run the following command:

```bash
python testcase_gen.py
```

This will generate a text file containing the testcase

## Running Helper

Make sure the helper.c, helper.h, solution.c and the generated testcase file are in the same directory. Run the following commands:

```bash
gcc solution.c -lpthread -o solution
gcc helper.c -lpthread -o helper

./helper <TESTCASE_NUMBER>
```

Where <TESTCASE_NUMBER> is as set in the variable test_case_number while generating the testcase
