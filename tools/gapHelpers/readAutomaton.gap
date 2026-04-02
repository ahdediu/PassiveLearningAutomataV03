#Read("/Users/adriandediu/CLionProjects/PassiveLearningAutomataV03/tools/gapHelpers/readAutomaton.gap");
MakeMooreLikeAutomaton := function(spec)
    local pos, left, outstr, nums, numInputs, numStates, transFlat,
          expected, i, j, k, transMatrix, outs, c, aut, labels, parts;

    pos := Position(spec, ':');
    if pos = fail then
        Error("Specification must contain ':' separating transitions and outputs");
    fi;

    left := spec{[1 .. pos-1]};
    outstr := spec{[pos+1 .. Length(spec)]};

    parts := SplitString(left, " ");
    nums := List(parts, x -> Int(x));

    if Length(nums) < 2 then
        Error("Need at least number of inputs and number of states");
    fi;

    numInputs := nums[1];
    numStates := nums[2];

    expected := numInputs * numStates;
    if Length(nums) <> 2 + expected then
        Error(
            Concatenation(
                "Expected ",
                String(expected),
                " transition numbers, but got ",
                String(Length(nums) - 2)
            )
        );
    fi;
    transFlat := nums{[3 .. Length(nums)]};

    # one row per input symbol, one column per state
    # source states are 0..n-1, GAP states are 1..n
    transMatrix := [];
    k := 1;
    for i in [1 .. numInputs] do
        transMatrix[i] := [];
        for j in [1 .. numStates] do
            transMatrix[i][j] := transFlat[k] + 1;
            k := k + 1;
        od;
    od;
        # parse outputs:
        # either packed symbolic form like "+-+-+"
        # or whitespace-separated integers like "1 2 3 4"
        parts := SplitString(outstr, " \n\r\t", "");
        parts := Filtered(parts, x -> Length(x) > 0);

        if Length(parts) = numStates then
            # integer outputs
            outs := List(parts, x -> Int(x));

        elif Length(parts) = 1 and Length(outstr) = numStates then
            # packed symbolic outputs
            outs := [];
            for i in [1 .. numStates] do
                outs[i] := outstr{[i..i]};
            od;

        else
            Error(
                Concatenation(
                    "Expected ",
                    String(numStates),
                    " outputs after ':', but got ",
                    String(Length(parts)),
                    " tokens and raw length ",
                    String(Length(outstr))
                )
            );
        fi;
    if numInputs=2 then
      numInputs:=["𝑎", "𝑏"];
    elif numInputs=4 then
      numInputs:=["𝑛", "𝑠", "𝑒", "𝑤" ];
    fi;
    aut := Automaton("det", numStates, numInputs, transMatrix, [1], []);


    labels := List([1 .. numStates], function ( s )
        return Concatenation( String(s - 1), ":", String(outs[s]) );
    end );

    return rec(
        automaton := aut,
        outputs := outs,
        labels := labels,
        transitionMatrix := transMatrix
    );
end;

#a := MakeMooreLikeAutomaton("2 2 1 0 0 1:+-"); #two_state_flip
#a := MakeMooreLikeAutomaton("2 3 1 2 0 2 0 1:ABC");# three_state_cycle
#a := MakeMooreLikeAutomaton("2 5 3 3 0 2 0 4 0 0 1 2:+-+-+");# test auto
#a := MakeMooreLikeAutomaton("2 3 1 2 0 2 1 0:5 7 9");# numeric example
#a:=MakeMooreLikeAutomaton("2 4 0 2 2 3 1 3 2 3:AABC"); #degree one
#a:=MakeMooreLikeAutomaton("2 4 1 1 3 3 1 1 3 3:ABCD"); # unreachable auto
a:=MakeMooreLikeAutomaton("2 7 1 3 4 5 6 5 6 2 4 3 5 6 5 0:RAABBCD"); #degree two
DrawAutomaton(a.automaton, a.labels);
#b := MakeMooreLikeAutomaton("4 86 33 1 3 4 5 5 7 7 9 10 11 11 13 13 15 16 17 17 19 19 21 22 23 24 24 26 26 28 29 30 31 31 1 34 35 36 37 38 39 40 40 41 42 44 45 46 47 48 49 50 51 51 52 54 54 56 57 57 59 59 61 61 63 64 64 66 66 68 68 70 71 71 73 73 75 75 77 78 78 80 80 82 82 84 85 85 0 1 2 3 4 5 32 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 24 18 19 20 21 22 23 24 32 25 26 35 27 28 29 30 31 33 41 42 44 45 46 47 48 49 50 51 44 46 47 49 50 51 58 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 71 72 73 74 75 76 77 78 0 32 2 2 3 4 6 6 8 8 9 10 12 12 14 14 15 16 18 18 20 20 21 22 23 25 25 27 27 28 29 30 32 0 33 34 35 36 37 38 39 41 42 43 43 44 45 46 47 48 49 50 52 53 53 55 55 56 58 58 60 60 62 62 63 65 65 67 67 69 69 70 72 72 74 74 76 76 77 79 79 81 81 83 83 84 0 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 25 26 27 28 29 30 31 33 34 36 37 38 39 40 6 41 34 35 36 37 38 39 40 42 43 43 52 45 53 54 48 55 56 57 59 60 61 62 63 64 65 66 67 68 69 70 71 72 73 74 75 76 77 78 79 80 81 82 83 84 85 79 80 81 82 83 84 85 :1 2 3 4 4 5 6 6 7 7 4 5 6 6 8 7 7 9 6 10 7 11 11 12 12 13 8 14 11 15 16 16 2 17 18 18 11 11 16 16 16 1 19 20 20 21 22 23 23 24 25 26 27 22 23 25 25 28 27 27 29 23 28 28 28 27 27 30 30 31 32 32 33 33 34 34 31 32 32 35 36 34 34 31 32 32");
#DrawAutomaton(b.automaton, b.labels);
