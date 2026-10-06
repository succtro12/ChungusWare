"""Audit only project-local static fictional resources. No external inputs."""
from pathlib import Path
from collections import Counter
import json, re
root=Path(__file__).parent
rows=json.loads((root/'haunted-lines.json').read_text(encoding='utf-8',errors='strict'))
expected={0:4200,1:1800,2:600,3:60,4:6}
assert len(rows)==6666 and dict(Counter(r['tier'] for r in rows))==expected
texts=[r['text'] for r in rows]
normalize=lambda s:re.sub(r'\s+',' ',s.lower().strip()).rstrip('.!?:;, -')
assert len(set(texts))==len(set(map(normalize,texts)))==6666
assert len({r['id'] for r in rows})==6666
families=set(r['family'] for r in rows)
assert len(families)==40
ordered_families=sorted(families)
header=(root.parent/'src'/'haunted_lines.h').read_text(encoding='utf-8')
compiled=re.findall(r'^\{0x([0-9a-f]+)ull,(\d+),(\d+),(".*")\},$',header,re.M)
assert len(compiled)==6666
for row,(stable_id,tier,family,text) in zip(rows,compiled):
 assert (row['id'],row['tier'],row['family'],row['text'])==(stable_id,int(tier),ordered_families[int(family)],json.loads(text))
assert 'return tier == 3 || tier == 4;' in header
assert 'hauntedBlackRed(uint8_t tier) { return tier == 4; }' in header
picker=(root.parent/'src'/'haunted_picker.h').read_text(encoding='utf-8')
assert not re.search(r'GetEnvironment|GetUserName|GetComputerName|GetSystemTime|GetLocalTime|filesystem|ifstream|ofstream|https?://|WinHttp|socket|clipboard|microphone|webcam',picker,re.I)
for r in rows:
 t=r['text']
 assert t and t==t.strip() and not re.search(r'\s{2,}',t),r
 assert t.isascii() and all(32<=ord(c)<=126 for c in t),r
 assert re.fullmatch(r'[a-z]+',r['family']) and r['family'] in families
 assert re.fullmatch(r'[0-9a-f]{16}',r['id'])
 assert not re.search(r'[{}$]|%[a-z_]+%|<[^>]+>|\\|https?://|\b(?:username|hostname|clipboard|webcam|microphone|ip address|serial number)\b',t,re.I),r
 assert not re.search(r'\b(?:undefined|null|nan)\b|[;:,]\s*[;:,]|\bthe the\b|\bis is\b',t,re.I),r
 assert len(t)<=116 and len(re.findall(r'[.!?](?:\s|$)',t))<=1,r
counts=Counter();bigrams=Counter();trigrams=Counter();prefixes=Counter();word_lengths=[];lengths=[]
for t in texts:
 words=re.findall(r"[a-z]+(?:'[a-z]+)?|[0-9]+",t.lower())
 counts[words[0]]+=1;word_lengths.append(len(words));lengths.append(len(t))
 bigrams.update(zip(words,words[1:]));trigrams.update(zip(words,words[1:],words[2:]));prefixes.update([' '.join(words[:3])])
forms=Counter(r['authoring_form'] for r in rows if not r['authoring_form'].startswith(('authored:','rare:','breach:','fragment:')))
assert max(forms.values())<100,forms.most_common(3)
assert max(prefixes.values())<67,prefixes.most_common(3)
assert max(trigrams.values())<100,trigrams.most_common(3)
top=lambda c:[{'phrase':' '.join(k) if isinstance(k,tuple) else k,'count':v} for k,v in c.most_common(30)]
report={'messages':len(rows),'tiers':dict(Counter(r['tier'] for r in rows)),
 'families':dict(sorted(Counter(r['family'] for r in rows).items())),
 'normalized_duplicates':0,'exact_duplicates':0,'stable_id_duplicates':0,
 'character_handling':'UTF-8 resource; printable ASCII text for existing font atlas',
 'length_characters':{'min':min(lengths),'max':max(lengths),'mean':sum(lengths)/len(lengths)},
 'word_count_buckets':dict(Counter('2-6' if 2<=n<=6 else '7-10' if n<=10 else '11-15' if n<=15 else '16+' for n in word_lengths)),
 'first_words':top(counts),'bigrams':top(bigrams),'trigrams':top(trigrams),'prefixes':top(prefixes),
 'authoring_forms':top(forms),'numeric_messages':sum(bool(re.search(r'\d',t)) for t in texts),
 'rare_style_count':sum(r['tier']==4 for r in rows),
 'rare_cooldown_count':sum(r['tier']>=3 for r in rows),
 'privacy':'Static corpus/header; picker reads only catalogue and session history. No interpolation or data hooks.',
 'limits':'Statistics are editorial checks, not a proof of perceptual quality. Complete text remains reviewable.'}
(root/'haunted-lines-audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
