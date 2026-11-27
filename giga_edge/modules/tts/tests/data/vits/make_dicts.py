#!/usr/bin/env python3
"""Writes the test dictionaries of the text stages from their sources next to them:
  text/ru/stress.dict      from text/ru/stress_words.txt: a word with '+' before each stressed vowel, "зам+ок"
  text/ru/yo.dict          from text/ru/yo_words.txt: a word with 'ё', found by its spelling with 'е'
  text/ru/homographs.dict  from text/ru/homograph_words.txt: the readings of a word, "з+амок зам+ок"; the
                           dictionary keeps the stressed vowels of all of them
  text/morph_test/morph.dict  from text/morph_test/source.xml, in the format of the OpenCorpora export
  glue_english/stress.dict, text_bad_english/stress.dict  the same as text/ru/stress.dict
  glue_english/english.dict  a header and one word: "aaa" read as "а"
  text_bad_stress/stress.dict, text_bad_english/english.dict  the dictionaries above with one byte changed after
                           the checksum: still well-formed, so only the check of the checksum finds it
The word dictionaries are in the format of text/stress_dict.h, the morph one of text/morph_dict.h, the English one
of text/english.h (all in the vits backend). Run from this directory after a source or a format changes.

  python3 make_dicts.py
"""
import struct
import xml.etree.ElementTree as ET
import zlib

LETTERS = '-абвгдеёжзийклмнопрстуфхцчшщъыьэюя'
CODE = {letter: code for code, letter in enumerate(LETTERS, 1)}
BLOCK_WORDS = 16


def lines(path):
    with open(path, encoding='utf-8') as source:
        return [line.strip() for line in source if line.strip() and not line.startswith('#')]


def marks_of(marked):
    """'зам+ок' -> ('замок', (3,))."""
    word, marks = '', []
    for symbol in marked:
        if symbol == '+':
            marks.append(len(word))
        else:
            word += symbol
    return word, tuple(marks)


def pack(magic, entries, record):
    """entries: [(letter codes, payload)]; a block holds BLOCK_WORDS words, each sharing a prefix with the previous one."""
    data, offsets = bytearray(), []
    for index, (codes, payload) in enumerate(sorted(entries)):
        if index % BLOCK_WORDS == 0:
            offsets.append(len(data))
            previous = b''
        shared = 0
        while shared < min(len(previous), len(codes)) and previous[shared] == codes[shared]:
            shared += 1
        data += bytes((shared, len(codes) - shared)) + codes[shared:] + record(payload)
        previous = codes
    body = struct.pack('<4s5I', magic, 1, len(entries), len(offsets), BLOCK_WORDS, len(data))
    body += struct.pack(f'<{len(offsets)}I', *offsets) + data
    return body + struct.pack('<I', zlib.adler32(body))


def word_dict(entries):
    """entries: {word: marked letter positions}; every mark but the last has the high bit set."""
    return pack(b'GEWD', [(bytes(CODE[letter] for letter in word), marks) for word, marks in entries.items()],
                lambda marks: bytes(mark | 0x80 for mark in marks[:-1]) + bytes(marks[-1:]))


def stress_entries():
    return dict(marks_of(line.split('\t')[0].lower()) for line in lines('text/ru/stress_words.txt'))


def yo_entries():
    words = [line.lower() for line in lines('text/ru/yo_words.txt')]
    return {word.replace('ё', 'е'): tuple(i for i, letter in enumerate(word) if letter == 'ё') for word in words}


def homograph_entries():
    entries = {}
    for line in lines('text/ru/homograph_words.txt'):
        readings = [marks_of(reading.lower()) for reading in line.split()]
        entries[readings[0][0]] = tuple(sorted({marks[0] for _, marks in readings}))
    return entries


# Analyses, u16: bits 0-1 the part, 2-4 the case, 5-6 the gender, 7-8 the number, 9 animate, 10 indeclinable,
# 11 plural only, 12 transitive.
PARTS = {'NOUN': 0, 'ADJF': 1, 'ADJS': 1, 'COMP': 1, 'PRTF': 1, 'PRTS': 1, 'VERB': 2, 'INFN': 2, 'GRND': 2}
CASES = {'nomn': 1, 'gent': 2, 'gen1': 2, 'gen2': 2, 'datv': 3, 'accs': 4, 'acc2': 4, 'ablt': 5, 'loct': 6,
         'loc1': 6, 'loc2': 6}
GENDERS = {'masc': 1, 'femn': 2, 'neut': 3}
NUMBERS = {'sing': 1, 'plur': 2}
# Errors, distortions and the vocative; names, surnames and patronymics ("машина" is not "Машин").
SKIPPED = {'Erro', 'Dist', 'voct', 'Name', 'Surn', 'Patr'}


def first(table, tags):
    return next((value for name, value in table.items() if name in tags), 0)


def analysis(part, tags, gender):
    return (part | first(CASES, tags) << 2 | gender << 5 | first(NUMBERS, tags) << 7 | ('anim' in tags) << 9
            | ('Fixd' in tags) << 10 | ('Pltm' in tags) << 11 | ('tran' in tags) << 12)


def morph_entries():
    entries = {}
    for lemma in ET.parse('text/morph_test/source.xml').getroot().iter('lemma'):
        base = {g.attrib['v'] for g in lemma.find('l').iter('g')}
        for form in lemma.iter('f'):
            word, tags = form.attrib['t'].lower(), base | {g.attrib['v'] for g in form.iter('g')}
            part = first(PARTS, tags) if tags & set(PARTS) else None
            if part is None or tags & SKIPPED or not set(word) <= set(LETTERS) or not 0 < len(word) <= 127:
                continue
            # A noun of common gender is both masculine and feminine.
            genders = (1, 2) if part == 0 and tags & {'ms-f', 'Ms-f'} else (first(GENDERS, tags),)
            entries.setdefault(word.replace('ё', 'е'), set()).update(analysis(part, tags, g) for g in genders)
    return pack(b'GEMD', [(bytes(CODE[letter] for letter in word), sorted(tags)[:255])
                          for word, tags in entries.items()],
                lambda tags: bytes((len(tags),)) + struct.pack(f'<{len(tags)}H', *tags))


def english_dict():
    body = '#vits english 7 windows 44 34 33 23 22 12 11 01 00 stress 4\naaa\tAH1 _ _\n'
    return (body + '#adler32 %08x\n' % zlib.adler32(body.encode('utf-8'))).encode('utf-8')


def damaged(data, at):
    data = bytearray(data)
    data[at] ^= 1
    return bytes(data)


def main():
    stress = word_dict(stress_entries())
    english = english_dict()
    files = {
        'text/ru/stress.dict': stress,
        'text/ru/yo.dict': word_dict(yo_entries()),
        'text/ru/homographs.dict': word_dict(homograph_entries()),
        'text/morph_test/morph.dict': morph_entries(),
        'glue_english/stress.dict': stress,
        'glue_english/english.dict': english,
        'text_bad_stress/stress.dict': damaged(stress, len(stress) - 5),  # the stress of the last word
        'text_bad_english/stress.dict': stress,
        'text_bad_english/english.dict': damaged(english, english.index(b'AH1') + 2),  # AH0
    }
    for path, data in files.items():
        with open(path, 'wb') as out:
            out.write(data)


if __name__ == '__main__':
    main()
