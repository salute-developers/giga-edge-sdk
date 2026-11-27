# Data of the text normalizer

`ru/` holds the tables of the normalizer (nouns, units, abbreviations, readings, service words, government), under
the licence of the code.

`ru/morph.dict` is optional: a morphological dictionary built from the dictionary of
[OpenCorpora](https://opencorpora.org) (CC BY-SA 3.0), ranked by the frequency list FrequencyWords (CC BY-SA 4.0).
A built dictionary is distributed under CC BY-SA with these sources named; it is not kept in the repository. Without
it the normalizer uses its tables and guesses by the ending.

`morph_test/` is a dictionary for the tests built the same way from `source.xml`, made up in the format of the
OpenCorpora export, under the licence of the code.

The length of a sentence (`max_sentence_chars`, 180 by default) is the measure of the system the acoustic model was
trained with: each word or mark is its letters and 2, a space before it 1 (none before `, : " )`).
