import sys
import re

def analyzefile(path, end=None, start=None):
	print(path);
	timestamp = re.compile("^\[(\d*)\]");
	speedreg  = re.compile("(\d+(\.\d+)?) MBytes");

	start = int(start) if start else None;
	end=int(end) if end else None;

	started = not start;
	speed = 0;
	values = 0;
	timeprinted = False;
	time = None;
	max = 0;
	min = float("infinity");

	with open(path) as file:
		for line in file:
			timestr = timestamp.search(line);

			if timestr:
				time = int(timestr.group(1));
				
				if start and start < time:
					started = True;

				if end and end < time:
					break;

				if not timeprinted:
					timeprinted = True;
					print("Starttime: ", time);

				continue;

			if not started:
				continue;

			speedstr = speedreg.search(line);

			if speedstr:
				curspeed = float(speedstr.group(1));
				speed += curspeed;
				values = values + 1;

				if curspeed > max:
					max=curspeed;
				if curspeed < min:
					min=curspeed;
				
	print("Endtime is ", time);
	print("Average speed is ", speed if values==0 else speed/values, "(", values , ")");
	print("Min: ", min," max: ", max);


if __name__ == "__main__":
	analyzefile(*sys.argv[1:]);