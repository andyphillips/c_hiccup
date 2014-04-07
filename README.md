c_hiccup
========

c_hiccup is a C implementation of jHiccup. Its useful if you want to
eliminate the JVM as a source of jitter. 

./c_hiccup -h
c_hiccup 1.000000
 c_hiccup [-d delay_time]  [-l identifier] [-r run_seconds] [-v] [-h]

-d delay_time  - time in milliseconds to wait before starting measurement
-l identifier  - output to identifier.hgrm and identifier.log instead
                 of default hiccup.hgrm and hiccup.log
-r run_seconds - run for a specific number of seconds. The default is
                 to run forever (3 years/2 tech refreshes)
-v             - verbose output. Display histogram summaries on stdout as
                 well as to files.
-h             - this text

Output should be compatible with the charting spreadsheet from jHiccup

By default, with no arguments, c_hiccup will run with an update
interval of 5 seconds, and a sample interval of 1 millisecond. 

It will output to hiccup.hgrm and hiccup.log. 

It relies on the HdrHistogram (packaged with this) from Mike Barker.

License is GPL. Tested on Mint 12 and Centos 6.4


