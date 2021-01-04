bad_chars = ['/']

with open('~/kaldi/egs/mycorpus/data/local/lang/lexicon.txt', "w") as f:
	test_string = ''.join(i for i in test_string if not i in bad_chars)
