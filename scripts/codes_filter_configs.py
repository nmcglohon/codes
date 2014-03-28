#!/usr/bin/python2.7

import configurator as conf
from sys import stdout
import argparse

def main():
    args = parse_args()

    # load and check the module
    mod = conf.import_from(args.substitute_py)
    conf.check_cfields(mod)

    # check that pairs are actually pairs
    tp = args.token_pairs
    if len(tp) % 2 != 0:
        raise ValueError("token pairs must come in twos")

    # intitialize the configuration object 
    cfg = conf.configurator(mod, [])

    # build a lookup structure for the pairs, converting to numbers if 
    # possible
    tdict = { }
    for i in range(0, len(tp), 2):
        v = conf.try_num(tp[i+1])
        if v == None:
            v = tp[i+1]
        tdict[tp[i]] = v

    # quick check - make sure pairs are actually in the config
    for k in tdict:
        if k not in cfg.replace_map:
            raise ValueError("key " + k + " not in original config")

    # hack - if we are using an output prefix, append the "dot" here
    if args.output_prefix == None:
        args.output_prefix = ""
    else:
        args.output_prefix = args.output_prefix + '.'
    # filter out the files
    # (cfg iterator returns nothing, eval'd for side effects)
    for i,_ in enumerate(cfg):
        for k in tdict:
            if tdict[k] != cfg.replace_map[k]:
                break
        else:
            stdout.write(args.output_prefix + str(i) + "\n")

def parse_args():
    parser = argparse.ArgumentParser(\
            description="emit configuration files to stdout matching given "
            "token values")
    parser.add_argument("-o", "--output_prefix", help="prefix of output config "
            "files (Default: print only the filtered indices)", type=str)
    parser.add_argument("substitute_py", 
            help="input python file given to codes_configurator.py - see "
            "codes_configurator.py for details")
    parser.add_argument("token_pairs", nargs='*',
            help="a list of whitespace-separated token, replace pairs to "
            "filter the set of generated configuration files by")
    return parser.parse_args()

if __name__ == "__main__":
    main()
