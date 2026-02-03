#!/usr/bin/env python3
"""
Kanata log diff tool for DESESC.

Compares two Kanata log files and reports the first instruction
where IF/WB/RN/EX/PNR/CO events happen at different cycles.
"""

import sys
import argparse
from collections import defaultdict
from dataclasses import dataclass
from typing import Dict, Set


@dataclass
class InstructionInfo:
    """Information about an instruction."""
    inst_id: int
    label: str = ""
    stages: Dict[str, int] = None  # stage -> cycle mapping

    def __post_init__(self):
        if self.stages is None:
            self.stages = {}


def parse_kanata_file(filename: str) -> Dict[int, InstructionInfo]:
    """
    Parse a Kanata log file and extract instruction stage timings.

    Returns a dict mapping instruction ID -> InstructionInfo with stage timings.
    """
    instructions = {}
    current_cycle = 0

    with open(filename, 'r') as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue

            parts = line.split('\t')
            if len(parts) < 2:
                continue

            cmd = parts[0]
            args = parts[1:]

            try:
                if cmd == 'C':
                    # Cycle marker: C <increment>
                    # Increment the current cycle
                    current_cycle += int(args[0])

                elif cmd == 'C=':
                    # Set cycle: C= <cycle>
                    current_cycle = int(args[0])

                elif cmd == 'I':
                    # Initialize instruction: I <id> <tid> <inum>
                    inst_id = int(args[0])
                    if inst_id not in instructions:
                        instructions[inst_id] = InstructionInfo(inst_id=inst_id)

                elif cmd == 'L':
                    # Label instruction: L <id> <tid> <instruction_text>
                    inst_id = int(args[0])
                    if inst_id not in instructions:
                        instructions[inst_id] = InstructionInfo(inst_id=inst_id)
                    # Join remaining parts as the label
                    instructions[inst_id].label = ' '.join(args[2:]) if len(args) > 2 else ""

                elif cmd == 'S':
                    # Start stage: S <id> <relative_cycle> <stage>
                    # The cycle is RELATIVE to current_cycle
                    if len(args) >= 3:
                        inst_id = int(args[0])
                        relative_cycle = int(args[1])
                        stage = args[2]

                        # Calculate absolute cycle
                        absolute_cycle = current_cycle + relative_cycle

                        if inst_id not in instructions:
                            instructions[inst_id] = InstructionInfo(inst_id=inst_id)

                        # Record the absolute cycle when this stage started
                        # Only keep the first occurrence if stage appears multiple times
                        if stage not in instructions[inst_id].stages:
                            instructions[inst_id].stages[stage] = absolute_cycle

            except (ValueError, IndexError) as e:
                print(f"Warning: Error parsing line {line_num} in {filename}: {line}", file=sys.stderr)
                print(f"  Error: {e}", file=sys.stderr)
                continue

    return instructions


def compare_instructions(file1: str, file2: str, stages_to_check: Set[str]):
    """
    Compare two Kanata log files and report first instruction with different timings.
    """
    print(f"Parsing {file1}...")
    inst1 = parse_kanata_file(file1)

    print(f"Parsing {file2}...")
    inst2 = parse_kanata_file(file2)

    print(f"\nFound {len(inst1)} instructions in {file1}")
    print(f"Found {len(inst2)} instructions in {file2}")

    # Get all instruction IDs from both files
    all_ids = sorted(set(inst1.keys()) | set(inst2.keys()))

    first_diff = None
    diff_count = 0

    for inst_id in all_ids:
        info1 = inst1.get(inst_id)
        info2 = inst2.get(inst_id)

        # Check if instruction exists in both files
        if info1 is None:
            print(f"\nInstruction {inst_id} missing in {file1}")
            if first_diff is None:
                first_diff = inst_id
            diff_count += 1
            continue

        if info2 is None:
            print(f"\nInstruction {inst_id} missing in {file2}")
            if first_diff is None:
                first_diff = inst_id
            diff_count += 1
            continue

        # Check if labels match (if available)
        if info1.label and info2.label and info1.label != info2.label:
            print(f"\nWarning: Instruction {inst_id} has different labels:")
            print(f"  {file1}: {info1.label}")
            print(f"  {file2}: {info2.label}")

        # Compare stage timings
        has_diff = False
        diff_details = []

        for stage in stages_to_check:
            cycle1 = info1.stages.get(stage)
            cycle2 = info2.stages.get(stage)

            if cycle1 != cycle2:
                has_diff = True
                diff_details.append((stage, cycle1, cycle2))

        if has_diff:
            if first_diff is None:
                first_diff = inst_id
                print(f"\n{'='*70}")
                print(f"FIRST DIFFERENCE FOUND")
                print(f"{'='*70}")
                print(f"Instruction ID: {inst_id}")
                if info1.label:
                    print(f"Instruction:    {info1.label}")
                print(f"\nStage timing differences:")
                print(f"{'Stage':<10} {file1:>20} {file2:>20}")
                print(f"{'-'*70}")

                for stage, c1, c2 in diff_details:
                    c1_str = str(c1) if c1 is not None else "N/A"
                    c2_str = str(c2) if c2 is not None else "N/A"
                    marker = " <-- DIFF" if c1 != c2 else ""
                    print(f"{stage:<10} {c1_str:>20} {c2_str:>20}{marker}")

                print(f"{'='*70}\n")

            diff_count += 1

    # Summary
    print(f"\nSummary:")
    print(f"  Total instructions checked: {len(all_ids)}")
    print(f"  Instructions with differences: {diff_count}")

    if first_diff is not None:
        print(f"  First difference at instruction ID: {first_diff}")
        return 1
    else:
        print(f"  No differences found!")
        return 0


def main():
    parser = argparse.ArgumentParser(
        description='Compare two Kanata log files and find first instruction with different stage timings.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s file1.kanata file2.kanata
  %(prog)s --stages IF,EX,WB file1.kanata file2.kanata
  %(prog)s -s IF -s EX file1.kanata file2.kanata
        """
    )

    parser.add_argument('file1', help='First Kanata log file')
    parser.add_argument('file2', help='Second Kanata log file')
    parser.add_argument(
        '-s', '--stages',
        action='append',
        help='Stages to check (can be specified multiple times). Default: IF,WB,RN,EX,PNR,CO'
    )

    args = parser.parse_args()

    # Determine which stages to check
    if args.stages:
        # Handle both comma-separated and individual -s flags
        stages = set()
        for stage_arg in args.stages:
            stages.update(s.strip() for s in stage_arg.split(','))
    else:
        stages = {'IF', 'WB', 'RN', 'EX', 'PNR', 'CO'}

    print(f"Checking stages: {', '.join(sorted(stages))}\n")

    try:
        return compare_instructions(args.file1, args.file2, stages)
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 2
    except Exception as e:
        print(f"Unexpected error: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 3


if __name__ == '__main__':
    sys.exit(main())
