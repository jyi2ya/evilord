local filename = arg[1]
local f = io.open(filename, "r")
if f == nil then
  return
end
local begin_pos = f:seek()
local end_pos = f:seek("end")
print(end_pos - begin_pos)
