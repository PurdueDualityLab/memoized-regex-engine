using System;
using System.Text.RegularExpressions;

using Newtonsoft.Json;

public class QueryCSharp
{
	static public void Main (string[] args)
	{
		// Read file
		string contents = System.IO.File.ReadAllText(args[0]);

		// Convert to JSON
		dynamic patternObj = JsonConvert.DeserializeObject(contents);

		string pattern = patternObj.pattern;
		int nPumps = Convert.ToInt32(patternObj.nPumps);

		TimeSpan matchTimeout = Regex.InfiniteMatchTimeout;
		if (patternObj.timeoutMS != null) {
			int timeoutMS = Convert.ToInt32(patternObj.timeoutMS);
			if (timeoutMS > 0) {
				matchTimeout = new TimeSpan(0, 0, 0, 0, timeoutMS);
				Console.Error.WriteLine("Using timeoutMS " + timeoutMS);
			} else {
				Console.Error.WriteLine("timeoutMS must be > 0, I will use no timeout instead");
			}
		}

		// Construct queryString
		Console.Error.WriteLine("Constructing query string");
		string queryString = "";
		foreach (dynamic pumpPair in patternObj.evilInput.pumpPairs) {
			// Use a doubling strategy -- much faster than iterating
			string pump = Convert.ToString(pumpPair.pump);
			int lengthOfExpandedPump = nPumps * pump.Length;
			string expandedPump = pump;
			if (lengthOfExpandedPump > 0) {
				while (expandedPump.Length < lengthOfExpandedPump) {
					// Console.Error.WriteLine("Doubling -- length " + expandedPump.Length + " < goal " + lengthOfExpandedPump);
					expandedPump += expandedPump;
				}
				expandedPump = expandedPump.Substring(0, lengthOfExpandedPump);
			} else {
				expandedPump = Convert.ToString(pumpPair.prefix);
			}

			queryString += pumpPair.prefix + expandedPump;
		}
		queryString += Convert.ToString(patternObj.evilInput.suffix);

		Console.Error.WriteLine("Issuing match request. Start your engines!");

		bool validPattern = true;
		try {
			// Construct regexp -- might throw
			// Isolate this so we're sure it's the pattern and not something else (e.g. TimeSpan)
			new Regex(pattern);
			Console.Error.WriteLine("Valid pattern");
		} catch (ArgumentException) {
			patternObj.exceptionString = "INVALID_INPUT";
			validPattern = false;
		}

		if (validPattern) {
			patternObj.inputLength = queryString.Length;
			try {
				// Attempt match
				//Console.Error.WriteLine("Attempting match: /" + pattern + "/, input " + queryString);
				Console.Error.WriteLine("Attempting match");
				int matched = 0;
				RegexOptions opts = RegexOptions.None;
				if (Regex.IsMatch(queryString, pattern, opts, matchTimeout)) {
					matched = 1;
				}

				patternObj.matched = matched;
				patternObj.exceptionString = "No exception";
			} catch (RegexMatchTimeoutException) {
				patternObj.exceptionString = "Regex match timed out";
				Console.Error.WriteLine(patternObj.exceptionString);
			}
		} else {
			patternObj.matched = false;
		}

		Console.WriteLine(JsonConvert.SerializeObject(patternObj));
	}
}
