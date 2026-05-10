# Passive Learning of Moore Automata: Reset, ResetX, and Back Algorithms

## 1. Model and Assumptions
- Moore automaton (states, alphabet Σ, output function λ)
- Learner is passive:
  - cannot choose inputs
  - only predicts outputs

## 2. Signature-based Learning

### Definition 1 (Signature Tree)
A tree storing outputs for paths up to depth d.

### Definition 2 (Bounded depth d)
Only paths of length ≤ d define canonical state identity.

### Definition 3 (State Status)
- Incomplete
- Complete
- Merged

## 3. Learner Automaton

### Invariants

**Invariant 1 (Canonical uniqueness)**  
Each complete signature corresponds to exactly one canonical state.

**Invariant 2 (Merged states inactive)**  
Merged states are never used as active states.

## 4. Reset Algorithm

Protocol:
- if output == "?"
    - learn
    - promote
    - reset
- else
    - advance

## 5. ResetX Algorithm

Adds:
- extended signature storage beyond depth d
- cascade promotion

### Lemma 1 (Extended reuse)
Information beyond depth d can be reused when creating child states.

## 6. Back Algorithm

Signals:
- ? = unknown output
- ! = backtracking signal

### Incomplete state decision
- if unknown → "?"
- if no missing leaf below → "! + output"
- else → output

### Complete state decision
- if closed → "! + output"
- else → output

## 7. Data Structures

### SignatureTree
- missing_count_
- missing_leaf_paths_

### LearnerAutomaton
- signature_to_state_
- closed_complete_

## 8. Lemmas

### Lemma 2 (Closed monotonicity)
Once a complete state is closed, it remains closed.

### Lemma 3 (Open states)
Open states are those that can reach incomplete states.

### Lemma 4 (Termination)
If no incomplete states exist, all states are closed.

### Lemma 5 (Back passivity)
Using "!" does not violate passivity.

## 9. Algorithms

### Reset
- ? → learn → promote → reset
- else → advance

### Back
- ? → learn → promote → back
- ! → back
- else → advance

## 10. Experimental Parameters
- Back_ℓ
- ResetX
- Reset
