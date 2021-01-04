# LDC package downloading tool

Bash script by Lane Schwartz (dowobeha@gmail.com)

based on Python code (https://github.com/jonmay/ldcdl) by Jonathan May (jonmay@isi.edu)

04 March 2016


# Requirements:

* bash
* curl
* sed
* tr
* LDC login/password


# Usage:

```
$ ./download-ldc-corpora LDC99T42
2016-26-04 16:26:49 UTC-06  Enter LDC login credentials                                       ...

Enter email address: you@somewhere.edu
Enter Password: 

2016-26-04 16:26:54 UTC-06  Accessing LDC login page                                          ...
2016-26-04 16:26:54 UTC-06  Logging in to your LDC account                                    ...
2016-26-04 16:26:55 UTC-06  Accessing list of your LDC corpora                                ...

A list of the LDC corpora associated with your account has been saved to corpora.tsv


2016-26-04 16:26:56 UTC-06  Downloading LDC99T42 from https://catalog.ldc.upenn.edu/download/...
```

Subsequent runs will use the credentials cached in cookies.txt, if that file exists.

So, to kick off a job in the background to download all of your LDC corpora:


```
$ ./download-ldc-corpora
2016-26-04 16:26:49 UTC-06  Enter LDC login credentials                                       ...

Enter email address: you@somewhere.edu
Enter Password: 

2016-26-04 16:26:54 UTC-06  Accessing LDC login page                                          ...
2016-26-04 16:26:54 UTC-06  Logging in to your LDC account                                    ...
2016-26-04 16:26:55 UTC-06  Accessing list of your LDC corpora                                ...

A list of the LDC corpora associated with your account has been saved to corpora.tsv


$ nohup ./download-ldc-corpora $(cut -f 1 corpora.tsv) &> download-ldc-corpora.log &

```


# Known Issues

* None
