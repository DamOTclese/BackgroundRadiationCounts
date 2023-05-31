# BackgroundRadiationCounts
Sample background radiation counts for Los Angeles County

The data being collected is from Geiger Counters located at various places within
the Angeles National Forest, typically along streams or in areas where particulate
pollutants accumulate.

The Geiger Counters are GMC-300E Geiger Muller Counter Data Loggers which are
small and mobile, requiring their battery packs to be recharged in the field from
hand-held chargers.

The data is collected to detect anomalies in high counts which happen due to a sweep
of water bringing radioactive waste past the counter, or particulate pollutants
with increased radioactive waste products, but there are counters placed along a
number of freeways which monitor for unreported transport of radioactive materials.

The typical data collected is normal background radiation which averages out to
about 20 counts per minute. Anomalous counts have peaked at around 330 count per
minute in a single minute (which is problematic to health) yet typically peak to
around 80 CPM which statistically we might see from normal alpha and beta decay
in rocks, or from a cosmic ray shower.

The source code to retrieve the data in binary, text, and comma-delimited format
is buggy yet it is used for hundreds of these counters placed throughout Los 
Angeles Country with the majority of them located in city rather than wild areas.

The standard disclaimers apply: The sdata is not intended to be an accurate
representation of anything, the source code is not maintained and its use is not
subject to misuse or problems stemming from its use.
