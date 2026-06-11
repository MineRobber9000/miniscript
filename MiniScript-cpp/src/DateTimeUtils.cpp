//
//  DateTimeUtils.cpp
//  MiniScript
//
//  Created by Joe Strout on 2/1/23.
//  Copyright © 2023 Joe Strout. All rights reserved.
//

#include "DateTimeUtils.h"
#include "MiniScript/SplitJoin.h"
#include <time.h>
#include <math.h>

#if _WIN32 || _WIN64
struct tm *localtime_r( const time_t *timer, struct tm *buf ) {
	struct tm *newtime = _localtime64(timer);
	if (newtime == nullptr) return nullptr;
	*buf = *newtime;
	return buf;
}
#endif

// cf. https://howardhinnant.github.io/date_algorithms.html
// we use an offset of 730425 (2000-01-01) instead of the typical 719468 (1970-01-01)
static int64_t epochOffsetDays = 730425;
static int64_t epochOffsetSeconds = 946684800;
int64_t days_from_civil(int y, int m, int d) {
	y -= m < 2;
	const int64_t era = (y >= 0 ? y : y-399) / 400;
	const unsigned yoe = static_cast<unsigned>(y - era * 400);   // [0, 399]
	const unsigned doy = (153*(m > 2 ? m-3 : m+9) + 2)/5 + d-1;  // [0, 365]
	const unsigned doe = yoe * 365 + yoe/4 - yoe/100 + doy;      // [0, 146096]
	return era * 146097 + static_cast<int64_t>(doe) - epochOffsetDays;
}

void civil_from_days(int64_t z, int& y, int& m, int& d) {
	z += epochOffsetDays;
	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = (unsigned)(z - era * 146097);  // [0, 146096]
	const unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
	y = (int)(yoe) + era * 400;
	const unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);
	const unsigned mp = (5*doy + 2)/153;
	d = doy - (153*mp + 2)/5 + 1;
	m = mp + (mp < 10 ? 3 : -9);
	y += (m <= 2);
}

// converts a struct tm into int64 seconds before/since 2000-01-01 00:00:00
// ignores DST and timezones (we weren't even parsing timezones anyways)
int64_t timestampFromTime(struct tm time) {
	int64_t days = days_from_civil(
		time.tm_year + 1900,
		time.tm_mon + 1,
		time.tm_mday
	);

	return days * 86400 +
		time.tm_hour * 3600 +
		time.tm_min * 60 +
		time.tm_sec;
}

// converts a timestamp from our epoch to a struct tm
struct tm timeFromTimestamp(int64_t timestamp) {
	struct tm time;

	int64_t days = timestamp / 86400;
	int64_t rem = timestamp % 86400;
	if (rem < 0) {
		rem += 86400;
		--days;
	}

	// convert days first
	civil_from_days(days, time.tm_year, time.tm_mon, time.tm_mday);
	time.tm_year -= 1900; // tm_year relative to 1900
	time.tm_mon -= 1; // tm_mon [0,11], not [1,12]

	// now time
	time.tm_hour = rem / 3600;
	time.tm_min = (rem / 60) % 60;
	time.tm_sec = rem % 60;

	// dst always 0 because UTC
	time.tm_isdst = 0;

	return time;
}

// gets the timestamp for the current second
int64_t timestampNow() {
	return time(NULL) - epochOffsetSeconds;
}

// gets the UTC timestamp for the given MiniScript timestamp
int64_t utcForTimestamp(int64_t timestamp) {
	return timestamp + epochOffsetSeconds;
}

namespace  MiniScript {

static bool Match(const String s, size_t *posB, const String match) {
	size_t matchLenB = match.LengthB();
	if (s.SubstringB(*posB, matchLenB) == match) {
		*posB += matchLenB;
		return true;
	}
	return false;
}

String FormatDate(time_t t, String formatSpec) {
	t = utcForTimestamp(t);
	tm dateTime;
	struct tm *newtime = gmtime_r(&t, &dateTime);
	if (newtime == nullptr) return "";  // arg t too large
	
	const int BUFSIZE = 128;
	char buffer[BUFSIZE];
	setlocale(LC_ALL, "");
	
	// First, check for special cases -- i.e. standard date formatters.
	// (Unless the first character is %, in which case skip that and
	// take the rest as an individual part, below.)
	// Note that "d", "g", and "G" are culture- and platform-specific,
	// and will not come out the same on different platforms.
	// All others have been implemented in standard ways assuming
	// an invariant culture (which may not match user expectations).
	if (formatSpec.empty()) {
		strftime(buffer, BUFSIZE, "%c", &dateTime);
		return buffer;
	}
	size_t posB = 0;
	if (formatSpec[0] == '%') {
		posB = 1;
	} else if (formatSpec == "d") {	// short date
		// culture- and platform-specific
		strftime(buffer, BUFSIZE, "%x", &dateTime);
		return buffer;
	} else if (formatSpec == "D") { // long date
		formatSpec = "dddd, dd MMMM yyyy";
	} else if (formatSpec == "f") { // full date/time (short time)
		formatSpec = "dddd, dd MMMM yyyy HH:mm";
	} else if (formatSpec == "F") { // full date/time (long time)
		formatSpec = "dddd, dd MMMM yyyy HH:mm:ss";
	} else if (formatSpec == "g") { // general date/time (short time)
		// culture- and platform-specific
		strftime(buffer, BUFSIZE, "%x ", &dateTime);
		String result = buffer;
		result += String::Format(dateTime.tm_hour) + ":";
		strftime(buffer, BUFSIZE, "%H %p", &dateTime);
		return result + String(buffer);
	} else if (formatSpec == "G") { // general date/time (long time)
		// culture- and platform-specific
		strftime(buffer, BUFSIZE, "%x %X", &dateTime);
		return buffer;
	} else if (formatSpec == "M" or formatSpec == "m") {	// month/day
		formatSpec = "d MMMM";
	} else if (formatSpec == "O" or formatSpec == "o") {	// round-trip (ISO) format
		formatSpec = "yyyy'-'MM'-'dd'T'HH':'mm':'ss'.'fffffffK";
	} else if (formatSpec == "R" or formatSpec == "r") {	// RFC1123 format
		formatSpec = "ddd, dd MMM yyyy HH':'mm':'ss 'GMT'";
	} else if (formatSpec == "s") {	// sortable
		formatSpec = "yyyy'-'MM'-'dd'T'HH':'mm':'ss";
	} else if (formatSpec == "t") { // short time
		formatSpec = "HH:mm";
	} else if (formatSpec == "T") { // long time
		formatSpec = "HH:mm:ss";
	} else if (formatSpec == "u") { // universal sortable
		formatSpec = "yyyy'-'MM'-'dd HH':'mm':'ss'Z'";
	} else if (formatSpec == "U") { // universal full date/time
		formatSpec = "dddd, dd MMMM yyyy HH:mm:ss";
	} else if (formatSpec == "Y" or formatSpec == "y") {	// year-month
		formatSpec = "yyyy MMMM";
	} else if (formatSpec.LengthB() == 1) {
		// invalid
		return "";
	}
	
	// Otherwise, parse individual parts.
	StringList result;
	size_t lenB = formatSpec.LengthB();
	while (posB < lenB) {
		if (Match(formatSpec, &posB, "yyyy")) {			// year
			result.Add(String::Format(dateTime.tm_year + 1900, "%04d"));
		} else if (Match(formatSpec, &posB, "yyy")) {
			result.Add(String::Format(dateTime.tm_year + 1900, "%03d"));
		} else if (Match(formatSpec, &posB, "yy")) {
			result.Add(String::Format(dateTime.tm_year % 100, "%02d"));
		} else if (Match(formatSpec, &posB, "MMMM")) {	// month name
			strftime(buffer, BUFSIZE, "%B", &dateTime);
			result.Add(buffer);
		} else if (Match(formatSpec, &posB, "MMM")) {	// month abbreviation
			strftime(buffer, BUFSIZE, "%b", &dateTime);
			result.Add(buffer);
		} else if (Match(formatSpec, &posB, "MM")) {	// month number
			result.Add(String::Format(dateTime.tm_mon + 1, "%02d"));
		} else if (Match(formatSpec, &posB, "M")) {
			result.Add(String::Format(dateTime.tm_mon + 1, "%d"));
		} else if (Match(formatSpec, &posB, "dddd")) {	// weekday name
			strftime(buffer, BUFSIZE, "%A", &dateTime);
			result.Add(buffer);
		} else if (Match(formatSpec, &posB, "ddd")) {	// weekday abbreviation
			strftime(buffer, BUFSIZE, "%a", &dateTime);
			result.Add(buffer);
		} else if (Match(formatSpec, &posB, "dd")) {	// day (number)
			result.Add(String::Format(dateTime.tm_mday, "%02d"));
		} else if (Match(formatSpec, &posB, "d")) {
			result.Add(String::Format(dateTime.tm_mday, "%d"));
		} else if (Match(formatSpec, &posB, "hh")) {	// 12-hour hour
			int twelveHourHour = (dateTime.tm_hour == 0 ? 12 : (dateTime.tm_hour - 1) % 12+1);
			result.Add(String::Format(twelveHourHour, "%02d"));
		} else if (Match(formatSpec, &posB, "h")) {
			int twelveHourHour = (dateTime.tm_hour == 0 ? 12 : (dateTime.tm_hour - 1) % 12+1);
			result.Add(String::Format(twelveHourHour, "%d"));
		} else if (Match(formatSpec, &posB, "HH")) {	// 24-hour hour
			result.Add(String::Format(dateTime.tm_hour, "%02d"));
		} else if (Match(formatSpec, &posB, "H")) {
			result.Add(String::Format(dateTime.tm_hour, "%d"));
		} else if (Match(formatSpec, &posB, "mm")) {	// minute
			result.Add(String::Format(dateTime.tm_min, "%02d"));
		} else if (Match(formatSpec, &posB, "m")) {
			result.Add(String::Format(dateTime.tm_min, "%d"));
		} else if (Match(formatSpec, &posB, "ss")) {	// second
			result.Add(String::Format(dateTime.tm_sec, "%02d"));
		} else if (Match(formatSpec, &posB, "s")) {
			result.Add(String::Format(dateTime.tm_sec, "%d"));
		} else if (Match(formatSpec, &posB, "ffffff")) {	// fractional part of seconds value
			double d, i;
			d = modf(t, &i);
			snprintf(buffer, BUFSIZE, "%.6f", d);
			result.Add(&buffer[2]);	// (skipping past "0.")
		} else if (Match(formatSpec, &posB, "fffff")) {
			double d, i;
			d = modf(t, &i);
			snprintf(buffer, BUFSIZE, "%.5f", d);
			result.Add(&buffer[2]);	// (skipping past "0.")
		} else if (Match(formatSpec, &posB, "ffff")) {
			double d, i;
			d = modf(t, &i);
			snprintf(buffer, BUFSIZE, "%.4f", d);
			result.Add(&buffer[2]);	// (skipping past "0.")
		} else if (Match(formatSpec, &posB, "fff")) {
			double d, i;
			d = modf(t, &i);
			snprintf(buffer, BUFSIZE, "%.3f", d);
			result.Add(&buffer[2]);	// (skipping past "0.")
		} else if (Match(formatSpec, &posB, "ff")) {
			double d, i;
			d = modf(t, &i);
			snprintf(buffer, BUFSIZE, "%.2f", d);
			result.Add(&buffer[2]);	// (skipping past "0.")
		} else if (Match(formatSpec, &posB, "f")) {
			double d, i;
			d = modf(t, &i);
			snprintf(buffer, BUFSIZE, "%.1f", d);
			result.Add(&buffer[2]);	// (skipping past "0.")
		} else if (Match(formatSpec, &posB, "FFFFFF")) {	// fractional part of seconds value, if nonzero
			double d, i;
			d = modf(t, &i);
			snprintf(buffer, BUFSIZE, "%.6f", d);
			String s(&buffer[2]);	// (skipping past "0.")
			if (s != "000000") result.Add(s);
		} else if (Match(formatSpec, &posB, "FFFFF")) {
			double d, i;
			d = modf(t, &i);
			snprintf(buffer, BUFSIZE, "%.5f", d);
			String s(&buffer[2]);	// (skipping past "0.")
			if (s != "00000") result.Add(s);
		} else if (Match(formatSpec, &posB, "FFFF")) {
			double d, i;
			d = modf(t, &i);
			snprintf(buffer, BUFSIZE, "%.4f", d);
			String s(&buffer[2]);	// (skipping past "0.")
			if (s != "0000") result.Add(s);
		} else if (Match(formatSpec, &posB, "FFF")) {
			double d, i;
			d = modf(t, &i);
			snprintf(buffer, BUFSIZE, "%.3f", d);
			String s(&buffer[2]);	// (skipping past "0.")
			if (s != "000") result.Add(s);
		} else if (Match(formatSpec, &posB, "FF")) {
			double d, i;
			d = modf(t, &i);
			snprintf(buffer, BUFSIZE, "%.2f", d);
			String s(&buffer[2]);	// (skipping past "0.")
			if (s != "00") result.Add(s);
		} else if (Match(formatSpec, &posB, "F")) {
			double d, i;
			d = modf(t, &i);
			snprintf(buffer, BUFSIZE, "%.1f", d);
			String s(&buffer[2]);	// (skipping past "0.")
			if (s != "0") result.Add(s);
		} else if (Match(formatSpec, &posB, "tt")) {	// AM/PM
			strftime(buffer, BUFSIZE, "%p", &dateTime);
			result.Add(buffer);
		} else if (Match(formatSpec, &posB, "t")) {
			strftime(buffer, BUFSIZE, "%p", &dateTime);
			buffer[1] = 0;	// (truncate to length 1)
			result.Add(buffer);
		} else if (Match(formatSpec, &posB, "gg") || Match(formatSpec, &posB, "g")) {
			if (dateTime.tm_year + 1900 > 0) result.Add("A.D.");
			else result.Add("B.C.");
		} else if (Match(formatSpec, &posB, ":")) {
			// Ugh.  Getting these in C is hard.  For now we're going to punt.
			result.Add(":");
		} else if (Match(formatSpec, &posB, "/")) {
			// See comment above.
			result.Add("/");
		} else if (formatSpec[posB] == '"') {
			// Find the closing quote, and output the contained string literal
			size_t endPosB = posB + 1;
			while (endPosB < lenB) {
				char c = formatSpec[endPosB];
				if (c == '\\') endPosB++;
				else if (c == '"') break;
			}
			result.Add(formatSpec.Substring(posB + 1, endPosB - endPosB - 1));
			posB = endPosB + 1;
		} else if (formatSpec[posB] == '\'') {
			// Find the closing quote, and output the contained string literal
			size_t endPosB = posB + 1;
			while (endPosB < lenB) {
				char c = formatSpec[endPosB];
				if (c == '\\') endPosB++;
				else if (c == '\'') break;
			}
			result.Add(formatSpec.Substring(posB + 1, endPosB - endPosB - 1));
			posB = endPosB + 1;
		} else if (Match(formatSpec, &posB, "\\") && posB < lenB) {
			result.Add(formatSpec[posB++]);
		} else {
			result.Add(formatSpec.SubstringB(posB, 1));
			posB++;
		}
	}
	
	return Join("", result);
}

// Parse a date/time consisting of a date, time, or date and time separated
// by one or more spaces.  Within a date, the separator is assumed to be '-'
// and the parts are assumed to be year, year-month, or year-month-day.
// Within a time, the separator is assumed to be ':' and the parts are assumed
// to be hour, hour:minute, or hour:minute:second.  If there is additionally
// a "P" or "PM" found in either the second field, or as a separate space-
// delimited part, then we add 12 to any hour values < 12.
//
// So basically this will parse a date in the format returned by default from
// FormatDate (which is also the same as a SQL date), or simple variations thereof.
time_t ParseDate(const String dateStr) {
	bool gotDate = false;
	bool pmTime = false;
	tm dateTime;
	memset(&dateTime, 0, sizeof(tm));

	StringList parts = Split(dateStr);
	for (long i=0, iLimit=parts.Count(); i<iLimit; i++) {
		String part = parts[i];
		if (part.Contains("-")) {
			// Parse a date
			StringList fields = Split(part, "-");
			dateTime.tm_year = fields[0].IntValue() - 1900;
			if (fields.Count() > 1) dateTime.tm_mon = fields[1].IntValue() - 1;
			if (fields.Count() > 2) dateTime.tm_mday = fields[2].IntValue();
			gotDate = true;
		} else if (part.Contains(":")) {
			// Parse a time
			StringList fields = Split(part, ":");
			dateTime.tm_hour = fields[0].IntValue();
			if (fields.Count() > 1) dateTime.tm_min = fields[1].IntValue();
			if (fields.Count() > 2) dateTime.tm_sec = fields[2].DoubleValue();
		} else {
			part = part.ToUpper();
			if (part == "P" || part == "PM") pmTime = true;
		}
	}
	if (pmTime && dateTime.tm_hour < 12) dateTime.tm_hour += 12;
	if (!gotDate) {
		// If no date is supplied, assume the current date
		tm now = timeFromTimestamp(timestampNow());
		dateTime.tm_year = now.tm_year;
		dateTime.tm_mon = now.tm_mon;
		dateTime.tm_mday = now.tm_mday;
	}
	return timestampFromTime(dateTime);
}

}

